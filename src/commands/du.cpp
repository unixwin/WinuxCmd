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
    // [GNU]
    OPTION("-a", "--all", "write counts for all files, not just directories"),
    // [GNU]
    OPTION("-A", "--apparent-size", "print apparent sizes"),
    // [GNU]
    OPTION("-B", "--block-size", "scale sizes by SIZE before printing them",
           STRING_TYPE),
    // [GNU]
    OPTION("-b", "--bytes", "equivalent to '--apparent-size --block-size=1'"),
    // [GNU]
    OPTION("-c", "--total", "produce a grand total"),
    // [GNU]
    OPTION(
        "-d", "--max-depth",
        "print the total for a directory only if it is N or fewer levels below",
        INT_TYPE),
    // [GNU]
    OPTION("-h", "--human-readable",
           "print sizes in powers of 1024 (e.g., 1023M)"),
    // [GNU]
    OPTION("-H", "--dereference-args",
           "dereference only symlinks that are command line arguments"),
    // [GNU]
    OPTION("", "--si", "print sizes in powers of 1000 (e.g., 1.1G)"),
    // [GNU]
    OPTION("-k", "", "like --block-size=1K"),
    // [GNU]
    OPTION("-L", "--dereference", "dereference all symbolic links"),
    // [GNU]
    OPTION("-P", "--no-dereference",
           "don't follow any symbolic links (default)"),
    // [GNU]
    OPTION("-s", "--summarize", "display only a total for each argument"),
    // [GNU]
    OPTION("-t", "--threshold", "exclude entries smaller/greater than SIZE",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--exclude", "exclude files that match PATTERN", STRING_TYPE),
    // [GNU]
    OPTION("-x", "--one-file-system",
           "skip directories on different file systems"),
    // [GNU]
    OPTION("", "--inodes",
           "list inode usage information instead of block usage"),
    // [GNU]
    OPTION("", "--time",
           "show time of the last modification of any file in the directory; "
           "see --time-style",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("", "--time-style", "show timestamps using STYLE", STRING_TYPE),
    // [GNU]
    OPTION("-0", "--null", "end each output line with NUL, not newline"),
    // [GNU]
    OPTION("-D", "--dereference-args",
           "dereference only symlinks that are command line arguments"),
    // [GNU]
    OPTION("", "--files0-from",
           "summarize disk usage of the NUL-terminated file names specified in "
           "file F",
           STRING_TYPE),
    // [GNU]
    OPTION("-l", "--count-links", "count sizes many times if hard linked"),
    // [GNU]
    OPTION("-m", "", "like --block-size=1M"),
    // [GNU]
    OPTION("-S", "--separate-dirs",
           "for directories, do not include size of subdirectories"),
    // [GNU]
    OPTION("-X", "--exclude-from",
           "exclude files that match any pattern in FILE", STRING_TYPE)};

// ======================================================
// Pipeline components
// ======================================================
namespace du_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Format size to human-readable string
 *
 * [GNU] Mirrors coreutils human_readable(): values round UP (ceiling) and
 * roll over to the next unit when the ceiling reaches the base (1537B ->
 * 1.6K, 1048575B -> 1.0M; Savannah #1154).
 *
 * @param size Size in bytes
 * @param si Use 1000-based units instead of 1024-based
 * @return Formatted string
 */
auto format_size(uint64_t size, bool si) -> std::string {
  const char* units = si ? "BKMGTPE"  // 1000-based
                         : "BKMGTP";  // 1024-based
  const uint64_t base = si ? 1000 : 1024;

  if (size < base) {
    return std::to_string(size);
  }

  // Smallest unit index (1 = K) with size in [base^idx, base^(idx+1)).
  int idx = 0;
  uint64_t unit = base;
  while (idx < 6 && size / unit >= base) {
    unit *= base;
    ++idx;
  }
  ++idx;

  // Ceiling in tenths of the selected unit (exact integer math; the
  // remainder-based form avoids overflow for exabyte-scale sizes).
  const uint64_t q = size / unit;
  const uint64_t r = size % unit;
  uint64_t tenths = q * 10 + (r * 10 + unit - 1) / unit;

  // Rollover: 1024.0K displays as 1.0M.
  while (tenths >= base * 10 && idx < 6) {
    tenths = (tenths + base - 1) / base;
    ++idx;
  }

  char buf[32];
  if (tenths < 100) {  // value < 10 units: one decimal digit
    snprintf(buf, sizeof(buf), "%llu.%llu%c",
             static_cast<unsigned long long>(tenths / 10),
             static_cast<unsigned long long>(tenths % 10), units[idx]);
  } else {
    snprintf(buf, sizeof(buf), "%llu%c",
             static_cast<unsigned long long>((tenths + 9) / 10), units[idx]);
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

// Parses a GNU-style block size spec. When |display_suffix| is non-null and
// the spec is a bare suffix without a leading integer (e.g. "M", "kB", "MiB"),
// the suffix text is stored there: GNU appends it to output sizes in that case
// ("--block-size=kB" displays 3000 as "3kB"), while an integer-prefixed spec
// ("1M") scales silently.
auto parse_block_size(std::string_view text,
                      std::string* display_suffix = nullptr)
    -> std::optional<uint64_t> {
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
  std::string_view matched_suffix;
  for (const auto& unit : units) {
    if (text.size() >= unit.suffix.size() &&
        text.substr(text.size() - unit.suffix.size()) == unit.suffix) {
      multiplier = unit.multiplier;
      number = text.substr(0, text.size() - unit.suffix.size());
      suffix_matched = true;
      matched_suffix = unit.suffix;
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
  if (display_suffix != nullptr) {
    *display_suffix = (suffix_matched && number.empty())
                          ? std::string(matched_suffix)
                          : std::string();
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

auto load_exclude_patterns_from_file(const std::string& path)
    -> cp::Result<std::vector<std::string>> {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open exclude file '" + path + "'");
  }

  std::string buf((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  if (buf.empty()) {
    return std::vector<std::string>{};
  }

  std::vector<std::string> lines;
  size_t start = 0;
  while (start < buf.size()) {
    size_t pos = buf.find('\n', start);
    if (pos == std::string::npos) {
      lines.emplace_back(buf.substr(start));
      break;
    }
    lines.emplace_back(buf.substr(start, pos - start));
    start = pos + 1;
  }

  for (auto& line : lines) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
  }
  return lines;
}

auto read_files0_from(const std::string& path)
    -> cp::Result<std::vector<std::string>> {
  std::istream* input = nullptr;
  std::ifstream file;
  if (path == "-") {
    input = &std::cin;
  } else {
    file.open(path, std::ios::binary);
    if (!file.is_open()) {
      return std::unexpected("cannot open file list '" + path + "'");
    }
    input = &file;
  }

  std::vector<std::string> paths;
  std::string name;
  while (std::getline(*input, name, '\0')) {
    if (!name.empty()) {
      paths.push_back(name);
    }
  }
  return paths;
}

auto ceil_div(uint64_t value, uint64_t divisor) -> uint64_t {
  if (value == 0) {
    return 0;
  }
  return 1 + ((value - 1) / divisor);
}

// [GNU] Canonical, case-insensitive key used to detect directories that were
// already traversed earlier in this invocation. GNU du skips such arguments
// entirely and lets already-traversed subtrees contribute nothing to later
// parent arguments (Savannah #10397).
auto du_dir_key(const std::wstring& path) -> std::wstring {
  std::error_code ec;
  std::filesystem::path canon =
      std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
  std::wstring key = ec ? path : canon.wstring();
  std::transform(key.begin(), key.end(), key.begin(), ::towupper);
  return key;
}

struct OutputConfig {
  bool human = false;
  bool si = false;
  uint64_t block_size = 1024;
  // Set when a command-line option selected the size mode; environment
  // block-size variables apply only when no option did (uutils #8916).
  bool size_explicit = false;
  // Suffix appended to scaled output (set when --block-size used a bare
  // unit suffix such as "M", e.g. "du -BM" prints "1M").
  std::string display_suffix;
};

enum class ThresholdMode { None, Minimum, Maximum };
enum class DuTimeMode { Modification, Access, Status };

struct DuConfig {
  bool count_all = false;
  bool total = false;
  bool summarize = false;
  bool dereference = false;       // -L
  bool dereference_args = false;  // -H/-D / --dereference-args
  bool apparent_size = false;     // -A / --apparent-size [DIFFERS]
  bool no_dereference = false;    // -P / --no-dereference [DIFFERS]
  bool count_links = false;       // -l / --count-links
  bool one_file_system = false;   // -x
  bool show_inodes = false;       // --inodes
  bool null_terminated = false;   // -0
  bool separate_dirs = false;     // -S
  bool show_time = false;         // --time
  DuTimeMode time_mode = DuTimeMode::Modification;
  std::string time_word;   // --time
  std::string time_style;  // --time-style
  int max_depth = -1;
  ThresholdMode threshold_mode = ThresholdMode::None;
  uint64_t threshold_size = 0;
  SmallVector<std::string, 16> exclude_patterns;
  OutputConfig output;
};

struct UsageSummary {
  uint64_t size = 0;
  FILETIME latest_time{};
  bool has_time = false;
};

auto print_record_terminator(bool null_terminated) -> void {
  if (null_terminated) {
    safePrint(char{'\0'});
  } else {
    safePrint("\n");
  }
}

auto print_scaled_size(uint64_t size, const OutputConfig& cfg) -> void {
  if (cfg.human || cfg.si) {
    safePrint(format_size(size, cfg.si));
    return;
  }

  safePrint(std::to_string(ceil_div(size, cfg.block_size)) +
            cfg.display_suffix);
}

auto parse_du_time_mode(std::string_view value) -> std::optional<DuTimeMode> {
  if (value.empty() || value == "mtime" || value == "modification" ||
      value == "modified") {
    return DuTimeMode::Modification;
  }
  if (value == "atime" || value == "access" || value == "use") {
    return DuTimeMode::Access;
  }
  if (value == "ctime" || value == "status") {
    return DuTimeMode::Status;
  }
  return std::nullopt;
}

auto get_selected_time(const WIN32_FILE_ATTRIBUTE_DATA& data, DuTimeMode mode)
    -> FILETIME {
  switch (mode) {
    case DuTimeMode::Modification:
      return data.ftLastWriteTime;
    case DuTimeMode::Access:
      return data.ftLastAccessTime;
    case DuTimeMode::Status:
      return data.ftCreationTime;
  }
  return data.ftLastWriteTime;
}

auto is_later_filetime(const FILETIME& lhs, const FILETIME& rhs) -> bool {
  return CompareFileTime(&lhs, &rhs) > 0;
}

auto update_latest_time(UsageSummary& summary, const FILETIME& candidate)
    -> void {
  if (!summary.has_time || is_later_filetime(candidate, summary.latest_time)) {
    summary.latest_time = candidate;
    summary.has_time = true;
  }
}

auto format_time_long_iso(FILETIME file_time,
                          std::string_view style = "long-iso") -> std::string {
  FILETIME local_ft{};
  if (!FileTimeToLocalFileTime(&file_time, &local_ft)) {
    local_ft = file_time;
  }

  SYSTEMTIME st{};
  if (!FileTimeToSystemTime(&local_ft, &st)) {
    if (style == "full-iso") {
      return "0000-01-01 00:00:00.000000000 +0000";
    }
    return "0000-00-00 00:00";
  }

  if (style == "full-iso") {
    // full-iso: "2025-01-01 12:34:56.789012345 +0800"
    TIME_ZONE_INFORMATION tzi{};
    GetTimeZoneInformation(&tzi);
    int tz_offset = -tzi.Bias;
    char buf[64];
    snprintf(buf, sizeof(buf),
             "%04d-%02d-%02d %02d:%02d:%02d.000000000 %+03d%02d", st.wYear,
             st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             tz_offset / 60, std::abs(tz_offset % 60));
    return std::string(buf);
  }

  if (style == "iso" || style == "posix") {
    // iso: "01-01 12:34" (same year) or "2025-01-01 12:34" (different year)
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char buf[32];
    if (st.wYear == now.wYear) {
      snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d", st.wMonth, st.wDay,
               st.wHour, st.wMinute);
    } else {
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear,
               st.wMonth, st.wDay, st.wHour, st.wMinute);
    }
    return std::string(buf);
  }

  if (style.starts_with("+")) {
    // Custom strftime format via +FORMAT
    // Map SYSTEMTIME fields to struct tm for strftime
    struct tm tm_val{};
    tm_val.tm_year = st.wYear - 1900;
    tm_val.tm_mon = st.wMonth - 1;
    tm_val.tm_mday = st.wDay;
    tm_val.tm_hour = st.wHour;
    tm_val.tm_min = st.wMinute;
    tm_val.tm_sec = st.wSecond;
    tm_val.tm_isdst = -1;
    std::string fmt(style.substr(1));
    char buf[128];
    size_t count = std::strftime(buf, sizeof(buf), fmt.c_str(), &tm_val);
    if (count > 0) {
      return std::string(buf);
    }
  }

  // default: long-iso style "2025-01-01 12:34"
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth,
           st.wDay, st.wHour, st.wMinute);
  return std::string(buf);
}

auto print_time_if_requested(const UsageSummary& summary, const DuConfig& cfg)
    -> void {
  if (!cfg.show_time || !summary.has_time) {
    return;
  }
  // [GNU] size and time are tab-separated (uutils #13267)
  safePrint("\t");
  safePrint(format_time_long_iso(summary.latest_time, cfg.time_style));
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
      output.size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-k") {
      output.human = false;
      output.si = false;
      output.block_size = 1024;
      output.size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-m") {
      output.human = false;
      output.si = false;
      output.block_size = 1024 * 1024;
      output.size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-h" || meta.long_name == "--human-readable") {
      output.human = true;
      output.si = false;
      output.size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.long_name == "--si") {
      output.human = false;
      output.si = true;
      output.size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-B" || meta.long_name == "--block-size") {
      auto value = std::get_if<std::string>(&occurrence.value);
      const char* opt_name = meta.short_name == "-B" ? "-B" : "--block-size";
      auto bad = [&]() {
        return std::unexpected(std::string("invalid ") + opt_name +
                               " argument '" +
                               (value ? *value : std::string()) + "'");
      };
      if (!value) {
        return bad();
      }
      if (*value == "human-readable") {
        output.human = true;
        output.si = false;
        output.size_explicit = true;
        output.display_suffix.clear();
        continue;
      }
      if (*value == "si") {
        output.human = false;
        output.si = true;
        output.size_explicit = true;
        output.display_suffix.clear();
        continue;
      }

      auto parsed = parse_block_size(*value, &output.display_suffix);
      if (!parsed) {
        return bad();
      }
      output.human = false;
      output.si = false;
      output.block_size = *parsed;
      output.size_explicit = true;
    }
  }

  // [GNU] With no size option, du reads its block size from the first set of
  // DU_BLOCK_SIZE, BLOCK_SIZE, BLOCKSIZE; "human-readable"/"si" select those
  // modes and an unparseable or zero value is ignored (uutils #8916).
  if (!output.size_explicit) {
    bool env_applied = false;
    for (const char* name : {"DU_BLOCK_SIZE", "BLOCK_SIZE", "BLOCKSIZE"}) {
      const char* value = std::getenv(name);
      if (value == nullptr) continue;
      const std::string spec(value);
      if (spec == "human-readable") {
        output.human = true;
        env_applied = true;
      } else if (spec == "si") {
        output.si = true;
        env_applied = true;
      } else if (auto parsed = parse_block_size(spec)) {
        output.block_size = *parsed;
        env_applied = true;
      }
      break;  // the first set variable wins even when unparseable
    }

    // [GNU] POSIXLY_CORRECT changes the default block size to 512B only when
    // neither an option nor an environment variable selected a size.
    if (!env_applied && std::getenv("POSIXLY_CORRECT") != nullptr) {
      output.block_size = 512;
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
  if (cfg.count_all && cfg.summarize) {
    return std::unexpected("cannot both summarize and show all");
  }
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

  for (const auto& occurrence :
       ctx.string_occurrences({"--exclude", "--exclude-from", "-X"})) {
    if (occurrence.long_name == "--exclude") {
      continue;
    }

    auto patterns = load_exclude_patterns_from_file(occurrence.value);
    if (!patterns) {
      return std::unexpected(patterns.error());
    }
    for (const auto& pattern : *patterns) {
      if (!pattern.empty()) {
        cfg.exclude_patterns.push_back(pattern);
      }
    }
  }

  cfg.dereference =
      ctx.get<bool>("--dereference", false) || ctx.get<bool>("-L", false);
  // -A / --apparent-size selects logical lengths; without it du counts
  // allocated blocks (see get_file_size). -b/--bytes implies apparent size.
  cfg.apparent_size =
      ctx.get<bool>("--apparent-size", false) || ctx.get<bool>("-A", false) ||
      ctx.get<bool>("--bytes", false) || ctx.get<bool>("-b", false);
  // -P / --no-dereference: Default behavior on Windows. [DIFFERS]
  cfg.no_dereference =
      ctx.get<bool>("--no-dereference", false) || ctx.get<bool>("-P", false);
  // -H / -D / --dereference-args: dereference only command-line symlink
  // operands.  Needed to report dangling link operands like GNU does.
  cfg.dereference_args = ctx.get<bool>("--dereference-args", false) ||
                         ctx.get<bool>("-H", false) ||
                         ctx.get<bool>("-D", false);
  // -l / --count-links: count sizes many times if hard linked
  cfg.count_links =
      ctx.get<bool>("--count-links", false) || ctx.get<bool>("-l", false);
  cfg.one_file_system =
      ctx.get<bool>("--one-file-system", false) || ctx.get<bool>("-x", false);
  cfg.show_inodes = ctx.get<bool>("--inodes", false);
  cfg.null_terminated =
      ctx.get<bool>("--null", false) || ctx.get<bool>("-0", false);
  cfg.separate_dirs =
      ctx.get<bool>("--separate-dirs", false) || ctx.get<bool>("-S", false);
  for (const auto& occurrence : ctx.string_occurrences({"--time"})) {
    auto parsed = parse_du_time_mode(occurrence.value);
    if (!parsed) {
      return std::unexpected("invalid time value");
    }
    cfg.show_time = true;
    cfg.time_word = occurrence.value;
    cfg.time_mode = *parsed;
  }

  // --time-style: format for time display
  for (const auto& occurrence : ctx.string_occurrences({"--time-style"})) {
    cfg.time_style = occurrence.value;
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
 * @brief Get hard link count for a file (Windows API)
 * @param path File path
 * @return Number of hard links, or 1 if unable to determine
 */
auto get_hard_link_count(const std::wstring& path) -> DWORD {
  HANDLE hFile = CreateFileW(
      path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return 1;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  DWORD result = 1;
  if (GetFileInformationByHandle(hFile, &info)) {
    result = info.nNumberOfLinks;
  }
  CloseHandle(hFile);
  return result;
}

/**
 * @brief Get a stable per-inode key (volume serial + file index)
 * @param path File path
 * @return L"<volume>:<index>" or empty if unavailable
 */
auto get_inode_key(const std::wstring& path) -> std::wstring {
  HANDLE hFile = CreateFileW(
      path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return L"";
  }
  BY_HANDLE_FILE_INFORMATION info{};
  std::wstring key;
  if (GetFileInformationByHandle(hFile, &info)) {
    const uint64_t index =
        (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    key = std::to_wstring(info.dwVolumeSerialNumber) + L":" +
          std::to_wstring(index);
  }
  CloseHandle(hFile);
  return key;
}

/**
 * @brief Get the allocation cluster size of the volume containing |path|.
 * @param path File path
 * @return Bytes per cluster, or 0 when it cannot be determined
 */
auto get_volume_cluster_size(const std::wstring& path) -> uint64_t {
  // Cache per volume root: du walks many files on the same volume.
  static std::unordered_map<std::wstring, uint64_t> cache;

  std::wstring root;
  if (path.size() >= 2 && path[1] == L':') {
    root = path.substr(0, 2) + L"\\";
  } else if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
    // UNC \\server\share\... -> root is \\server\share + backslash
    size_t server_end = path.find(L'\\', 2);
    if (server_end != std::wstring::npos) {
      size_t share_end = path.find(L'\\', server_end + 1);
      if (share_end != std::wstring::npos) {
        root = path.substr(0, share_end + 1);
      }
    }
  }
  if (root.empty()) {
    // Relative path: resolve against the current drive.
    wchar_t cwd[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, cwd) == 0 || cwd[1] != L':') {
      return 0;
    }
    root = std::wstring(cwd, 2) + L"\\";
  }

  if (auto it = cache.find(root); it != cache.end()) {
    return it->second;
  }
  DWORD sectors_per_cluster = 0;
  DWORD bytes_per_sector = 0;
  DWORD unused = 0;
  uint64_t cluster = 0;
  if (GetDiskFreeSpaceW(root.c_str(), &sectors_per_cluster, &bytes_per_sector,
                        &unused, &unused)) {
    cluster = static_cast<uint64_t>(sectors_per_cluster) * bytes_per_sector;
  }
  cache[root] = cluster;
  return cluster;
}

/**
 * @brief Get file size
 * @param path File path
 * @param apparent When true return the logical size; otherwise return the
 *                 on-disk allocated size like GNU du's st_blocks accounting
 * @return File size or 0 if error
 */
auto get_file_size(const std::wstring& path, bool apparent) -> uint64_t {
  if (!apparent) {
    // [GNU] du counts allocated blocks (st_blocks), not the logical length.
    // GetCompressedFileSizeW accounts for compression and sparse regions;
    // round up to the volume cluster to model block allocation.
    ULARGE_INTEGER allocated{};
    allocated.LowPart =
        GetCompressedFileSizeW(path.c_str(), &allocated.HighPart);
    if (allocated.LowPart != INVALID_FILE_SIZE || GetLastError() == NO_ERROR) {
      const uint64_t cluster = get_volume_cluster_size(path);
      if (cluster > 0) {
        return (allocated.QuadPart + cluster - 1) / cluster * cluster;
      }
      return allocated.QuadPart;
    }
  }
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
                        std::unordered_map<std::wstring, FILETIME>& times,
                        int current_depth, const DuConfig& cfg,
                        std::unordered_set<std::wstring>& seen_inodes,
                        std::unordered_set<std::wstring>& visited_dirs,
                        const std::wstring& root_drive = L"",
                        // [GNU] Entries are printed in traversal post-order:
                        // a directory line follows its subtree, so the operand
                        // directory prints last (Savannah #13956).
                        std::vector<std::wstring>* print_order = nullptr)
    -> UsageSummary {
  WIN32_FIND_DATAW find_data;
  std::wstring search_path = path + L"\\*";
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return {};
  }

  // Determine root drive for --one-file-system
  std::wstring drive = root_drive;
  if (drive.empty() && path.size() >= 2 && path[1] == L':') {
    drive = path.substr(0, 2);
  }

  UsageSummary summary;
  WIN32_FILE_ATTRIBUTE_DATA path_data{};
  const bool have_path_data =
      cfg.show_time &&
      GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &path_data);

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

    // --one-file-system: skip directories on different drives
    if (cfg.one_file_system && !drive.empty() && full_path.size() >= 2 &&
        full_path[1] == L':') {
      std::wstring file_drive = full_path.substr(0, 2);
      if (file_drive != drive) {
        continue;
      }
    }

    const int child_depth = current_depth + 1;

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // [GNU] Subtrees already traversed under an earlier argument contribute
      // nothing to this parent (Savannah #10397).
      const std::wstring child_key = du_dir_key(full_path);
      if (visited_dirs.count(child_key) != 0) {
        continue;
      }
      visited_dirs.insert(child_key);
      // --dereference: on Windows, follow junction points
      // (FindFirstFile already handles this for most cases)

      // Recursively calculate subdirectory size
      UsageSummary child_summary =
          calculate_dir_size(full_path, sizes, times, child_depth, cfg,
                             seen_inodes, visited_dirs, drive, print_order);
      if (!cfg.separate_dirs) {
        summary.size += child_summary.size;
      }
      if (child_summary.has_time) {
        update_latest_time(summary, child_summary.latest_time);
      }
    } else {
      // It's a file
      uint64_t file_size = get_file_size(full_path, cfg.apparent_size);
      // [GNU] Hard-linked files are counted once per invocation; later
      // occurrences of the same inode contribute nothing (uutils #9202
      // #10241 #10312 #9871). --count-links opts out.
      bool counted = true;
      if (!cfg.count_links) {
        const DWORD nlinks = get_hard_link_count(full_path);
        if (nlinks > 1) {
          const std::wstring inode_key = get_inode_key(full_path);
          if (!inode_key.empty()) {
            if (!seen_inodes.insert(inode_key).second) {
              counted = false;
              file_size = 0;
            }
          }
        }
      } else {
        const DWORD nlinks = get_hard_link_count(full_path);
        if (nlinks > 1) {
          file_size *= nlinks;
        }
      }
      summary.size += file_size;
      if (cfg.show_time) {
        WIN32_FILE_ATTRIBUTE_DATA file_data{};
        if (GetFileAttributesExW(full_path.c_str(), GetFileExInfoStandard,
                                 &file_data)) {
          FILETIME file_time = get_selected_time(file_data, cfg.time_mode);
          update_latest_time(summary, file_time);
          if (cfg.count_all && counted &&
              (cfg.max_depth < 0 || child_depth <= cfg.max_depth)) {
            times[full_path] = file_time;
          }
        }
      }

      // [GNU] --inodes counts entries instead of blocks: each file counts
      // one (extra links under --count-links); a directory adds its whole
      // subtree via the recursion above.  The counts travel through the
      // same summary.size/sizes plumbing used for byte sizes.
      if (cfg.show_inodes) {
        if (cfg.count_links) {
          summary.size += std::max<DWORD>(get_hard_link_count(full_path), 1);
        } else if (counted) {
          summary.size += 1;
        }
      }

      // Count individual files if requested
      if (counted && cfg.count_all &&
          (cfg.max_depth < 0 || child_depth <= cfg.max_depth)) {
        sizes[full_path] = file_size;
        if (print_order != nullptr) {
          print_order->push_back(full_path);
        }
      }
    }
  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  if (cfg.max_depth < 0 || current_depth <= cfg.max_depth) {
    sizes[path] = summary.size;
    if (print_order != nullptr) {
      print_order->push_back(path);
    }
    if (cfg.show_time && summary.has_time) {
      times[path] = summary.latest_time;
    }
  }

  if (cfg.show_time && !summary.has_time && have_path_data) {
    update_latest_time(summary, get_selected_time(path_data, cfg.time_mode));
    if (cfg.max_depth < 0 || current_depth <= cfg.max_depth) {
      times[path] = summary.latest_time;
    }
  }

  return summary;
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

  const std::string files0_from = ctx.get<std::string>("--files0-from", "");
  if (!files0_from.empty()) {
    if (!ctx.positionals.empty()) {
      return std::unexpected(
          "--files0-from disallows processing paths named on the command line");
    }
    auto file_list = read_files0_from(files0_from);
    if (!file_list) {
      return std::unexpected(file_list.error());
    }
    for (const auto& path : *file_list) {
      paths.push_back(path);
    }
  } else if (ctx.positionals.empty()) {
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
  // [GNU] Hard-link dedup spans all arguments of one invocation.
  std::unordered_set<std::wstring> seen_inodes;
  // [GNU] Directory arguments already traversed earlier in this invocation
  // are skipped entirely: no print, no recount (Savannah #10397).
  std::unordered_set<std::wstring> visited_dirs;

  // [GNU] du prints operand paths and their descendants with forward
  // slashes (Savannah #13956).
  const auto display_path = [](const std::wstring& p) -> std::string {
    std::string display = wstring_to_utf8(p);
    std::replace(display.begin(), display.end(), '\\', '/');
    return display;
  };

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    std::wstring wpath = utf8_to_wstring(path);

    // [GNU] Under -L/-H a symlink operand that does not resolve (dangling
    // link) is reported and the run fails; GNU prints no errno text for the
    // dangling case but "No such file or directory" for a missing path.
    if (cfg.dereference || cfg.dereference_args) {
      std::error_code ec;
      (void)std::filesystem::status(std::filesystem::path(wpath), ec);
      if (ec) {
        std::error_code lec;
        const bool operand_exists = std::filesystem::exists(
            std::filesystem::symlink_status(std::filesystem::path(wpath), lec));
        safeErrorPrint("du: cannot access '");
        safeErrorPrint(path);
        if (!lec && operand_exists) {
          safeErrorPrint("'\n");
        } else {
          safeErrorPrint("': No such file or directory\n");
        }
        all_ok = false;
        continue;
      }
    }

    // Check if path exists.  Without -L/-H du lstats the operand, so a
    // dangling symlink still counts (as a zero-size entry) like GNU -P.
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      std::error_code lec;
      const auto link_status =
          std::filesystem::symlink_status(std::filesystem::path(wpath), lec);
      if (!lec && std::filesystem::is_symlink(link_status) &&
          !cfg.dereference && !cfg.dereference_args) {
        attrs = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_REPARSE_POINT;
      } else {
        safeErrorPrint("du: cannot access '");
        safeErrorPrint(path);
        safeErrorPrint("': No such file or directory\n");
        all_ok = false;
        continue;
      }
    }

    if (should_exclude(cfg, wpath,
                       std::filesystem::path(wpath).filename().wstring())) {
      continue;
    }

    std::unordered_map<std::wstring, uint64_t> sizes;
    std::unordered_map<std::wstring, FILETIME> times;

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
      // [GNU] Skip an argument directory that was already traversed under an
      // earlier argument (Savannah #10397).
      const std::wstring arg_key = du_dir_key(wpath);
      if (!visited_dirs.insert(arg_key).second) {
        continue;
      }
      // [GNU] Entries print in traversal post-order: every directory line
      // follows its subtree, so the operand directory prints last
      // (Savannah #13956).
      std::vector<std::wstring> order;
      // Calculate directory size
      UsageSummary dir_summary = calculate_dir_size(
          wpath, sizes, times, 0, cfg, seen_inodes, visited_dirs, L"", &order);

      // Print directory size
      uint64_t dir_size = sizes[wpath];
      grand_total += dir_size;

      auto print_entry = [&](const std::wstring& entry_path, uint64_t size,
                             bool is_operand_root) {
        safePrint(L"");
        if (cfg.show_inodes) {
          // [GNU] --inodes: print each entry's recursive entry count, not
          // a literal 1 (only standalone file operands are exactly 1).
          (void)is_operand_root;
          safePrint(std::to_string(size));
        } else {
          print_scaled_size(size, cfg.output);
        }
        if (cfg.show_time) {
          UsageSummary entry_summary;
          auto time_it = times.find(entry_path);
          if (time_it != times.end()) {
            entry_summary.latest_time = time_it->second;
            entry_summary.has_time = true;
          }
          if (is_operand_root && dir_summary.has_time) {
            entry_summary.latest_time = dir_summary.latest_time;
            entry_summary.has_time = true;
          }
          print_time_if_requested(entry_summary, cfg);
        }
        safePrint("\t");
        safePrint(display_path(entry_path));
        print_record_terminator(cfg.null_terminated);
      };

      if (cfg.summarize) {
        // -s: print only the operand total.
        if (passes_threshold(cfg, dir_size)) {
          print_entry(wpath, dir_size, true);
        }
      } else {
        for (const auto& subpath : order) {
          auto size_it = sizes.find(subpath);
          if (size_it == sizes.end()) {
            continue;
          }
          if (!passes_threshold(cfg, size_it->second)) {
            continue;
          }
          print_entry(subpath, size_it->second, subpath == wpath);
        }
        // Fallback safety: the operand root always prints.
        if (order.empty() ||
            std::find(order.begin(), order.end(), wpath) == order.end()) {
          if (passes_threshold(cfg, dir_size)) {
            print_entry(wpath, dir_size, true);
          }
        }
      }
    } else {
      // It's a file
      uint64_t file_size = get_file_size(wpath, cfg.apparent_size);
      // --count-links: multiply by hard link count [DIFFERS]
      if (cfg.count_links) {
        DWORD nlinks = get_hard_link_count(wpath);
        if (nlinks > 1) {
          file_size *= nlinks;
        }
      }
      grand_total += file_size;

      if (passes_threshold(cfg, file_size)) {
        UsageSummary file_summary;
        if (cfg.show_time) {
          WIN32_FILE_ATTRIBUTE_DATA file_data{};
          if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard,
                                   &file_data)) {
            file_summary.latest_time =
                get_selected_time(file_data, cfg.time_mode);
            file_summary.has_time = true;
          }
        }
        safePrint(L"");
        if (cfg.show_inodes) {
          safePrint("1");  // Each file is 1 inode
        } else {
          print_scaled_size(file_size, cfg.output);
        }
        print_time_if_requested(file_summary, cfg);
        safePrint("\t");
        safePrint(display_path(wpath));
        print_record_terminator(cfg.null_terminated);
      }
    }
  }

  if (cfg.total && passes_threshold(cfg, grand_total)) {
    safePrint(L"");
    if (cfg.show_inodes) {
      safePrint(std::to_string(grand_total));
    } else {
      print_scaled_size(grand_total, cfg.output);
    }
    safePrint("\ttotal");
    print_record_terminator(cfg.null_terminated);
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
