// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
  // [L2 self-parse] POSIX drive spellings resolve through the shared
  // operand boundary like the traversal operands do (#1174).
  std::ifstream in(native_path::normalize_api_operand(path), std::ios::binary);
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

// [GNU] --files0-from diagnostics (du.c:1018-1115): an unopenable
// list is fatal with "cannot open '<F>' for reading: <reason>"; an
// empty name or (when reading stdin) a name of '-' is diagnosed with
// the list name and record number, marks the run failed, and
// processing continues. Both cases used to be silent here.
auto read_files0_from(const std::string& path, bool* names_ok)
    -> cp::Result<std::vector<std::string>> {
  std::istream* input = nullptr;
  std::ifstream file;
  if (path == "-") {
    file_io::set_stdin_binary();
    input = &std::cin;
  } else {
    // [L2 self-parse] POSIX drive spellings resolve through the shared
    // operand boundary like the traversal operands do (#1174).
    const std::wstring api_path = native_path::normalize_api_operand(path);
    file.open(api_path, std::ios::binary);
    if (!file.is_open()) {
      // GNU appends the errno text; recover it with a probe open.
      std::string reason = "No such file or directory";
      const HANDLE probe = CreateFileW(api_path.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
      if (probe == INVALID_HANDLE_VALUE) {
        reason = winux::win32_error::win32_posix_error_text(GetLastError());
      } else {
        CloseHandle(probe);
      }
      return std::unexpected("cannot open '" + path + "' for reading: " +
                             reason);
    }
    input = &file;
  }

  std::vector<std::string> paths;
  std::string name;
  uint64_t record = 0;
  while (std::getline(*input, name, '\0')) {
    ++record;
    if (name.empty()) {
      safeErrorPrint("du: ");
      safeErrorPrint(path);
      safeErrorPrint(":");
      safeErrorPrint(std::to_string(record));
      safeErrorPrint(": invalid zero-length file name\n");
      if (names_ok != nullptr) *names_ok = false;
      continue;
    }
    if (path == "-" && name == "-") {
      safeErrorPrint(
          "du: when reading file names from stdin, no file name of "
          "'-' allowed\n");
      if (names_ok != nullptr) *names_ok = false;
      continue;
    }
    paths.push_back(name);
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
  // #1288: canonicalize the extended spelling so the key is computed,
  // not degraded to the raw literal, for directories past MAX_PATH.
  // Keys stay uniform within a run because every directory passes
  // through this one function.
  std::filesystem::path canon = std::filesystem::weakly_canonical(
      std::filesystem::path(native_path::to_extended_path(path)), ec);
  std::wstring key = ec ? path : canon.wstring();
  // Some STLs skip lexical normalization for verbatim (\\?\) paths and
  // keep a trailing "/." segment or separator; strip both so spellings
  // like `dir` and `dir/.` still key identically. A real final component
  // that merely ends in a dot (e.g. `inner.`) is untouched: only a whole
  // trailing "." segment is removed.
  while (key.size() > 1 && (key.back() == L'\\' || key.back() == L'/')) {
    key.pop_back();
  }
  while (key.size() > 2 && key[key.size() - 1] == L'.' &&
         (key[key.size() - 2] == L'\\' || key[key.size() - 2] == L'/')) {
    key.resize(key.size() - 2);
    while (key.size() > 1 && (key.back() == L'\\' || key.back() == L'/')) {
      key.pop_back();
    }
  }
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
  // [GNU] The total WITHOUT --separate-dirs pruning: du -Sc reports
  // the grand total of everything counted, while -S only changes
  // what each printed directory line contains (du.c:654 accumulates
  // tot_dui from every file's dui_to_print, -S or not).
  uint64_t full_size = 0;
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

// [GNU] The --time word universe (du.c, time_args/time_types):
// atime/access/use select access time, ctime/status select status
// time, mtime (also the bare --time default) selects modification
// time. argmatch semantics: unique prefixes match, ambiguous or
// unknown words fail. The applet used to also accept the non-GNU
// words 'modification' and 'modified' and rejected all prefixes.
auto parse_du_time_mode(std::string_view value) -> std::optional<DuTimeMode> {
  if (value.empty()) {
    return DuTimeMode::Modification;
  }
  struct Word {
    std::string_view text;
    DuTimeMode mode;
  };
  static constexpr Word words[] = {
      {"atime", DuTimeMode::Access}, {"access", DuTimeMode::Access},
      {"use", DuTimeMode::Access},   {"ctime", DuTimeMode::Status},
      {"status", DuTimeMode::Status}, {"mtime", DuTimeMode::Modification},
  };
  std::optional<DuTimeMode> matched;
  size_t matches = 0;
  for (const Word& word : words) {
    if (word.text == value) {
      return word.mode;
    }
    if (word.text.starts_with(value)) {
      matched = word.mode;
      ++matches;
    }
  }
  if (matches == 1) {
    return matched;
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
    // [GNU] du --time-style=iso prints the DATE ONLY ("%Y-%m-%d");
    // the same-year/time-of-day elision this branch used to do is
    // ls's iso style, not du's.
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", st.wYear, st.wMonth,
             st.wDay);
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
    // [GNU] du.c:937-938: "...show all entries" (the noun was dropped).
    return std::unexpected("cannot both summarize and show all entries");
  }
  if (ctx.get<int>("-d", -1) != -1) {
    cfg.max_depth = ctx.get<int>("-d", -1);
  }
  // [GNU] -s with --max-depth (du.c:940-947): depth 0 warns and
  // proceeds; any other explicit depth warns and fails with usage.
  if (cfg.summarize && cfg.max_depth >= 0) {
    if (cfg.max_depth == 0) {
      safeErrorPrint(
          "du: warning: summarizing is the same as using "
          "--max-depth=0\n");
    } else {
      return std::unexpected("warning: summarizing conflicts with "
                             "--max-depth=" +
                             std::to_string(cfg.max_depth));
    }
  }

  auto output = configure_output(ctx);
  if (!output) {
    return std::unexpected(output.error());
  }
  cfg.output = *output;

  for (const auto& occurrence : ctx.string_occurrences({"--threshold", "-t"})) {
    auto threshold = parse_threshold_size(occurrence.value);
    // [GNU] du.c:841-849: '-0' is neither negative nor positive and
    // is rejected by name, as is any unparseable value.
    if (!threshold || (occurrence.value.starts_with("-") && *threshold == 0)) {
      return std::unexpected("invalid --threshold argument '" +
                             std::string(occurrence.value) + "'");
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
      // [GNU] XARGMATCH failure shape for --time.
      return std::unexpected(
          "invalid argument '" + std::string(occurrence.value) +
          "' for '--time'\nValid arguments are:\n"
          "  - 'atime', 'access', 'use'\n"
          "  - 'ctime', 'status'\n"
          "  - 'mtime'");
    }
    cfg.show_time = true;
    cfg.time_word = occurrence.value;
    cfg.time_mode = *parsed;
  }

  // [GNU] --time-style is validated against the GNU style set and
  // defaults from the TIME_STYLE environment variable (du.c:
  // 960-1005); 'posix-' prefixes select the same formats. This used
  // to be stored unvalidated, so a bogus style silently formatted.
  for (const auto& occurrence : ctx.string_occurrences({"--time-style"})) {
    cfg.time_style = occurrence.value;
  }
  if (cfg.time_style.empty()) {
    if (const char* env_style = std::getenv("TIME_STYLE")) {
      cfg.time_style = env_style;
    }
  }
  if (!cfg.time_style.empty() && !cfg.time_style.starts_with("+")) {
    std::string_view style = cfg.time_style;
    if (style.starts_with("posix-")) style.remove_prefix(6);
    if (style != "full-iso" && style != "long-iso" && style != "iso" &&
        style != "locale") {
      return std::unexpected("invalid argument '" + cfg.time_style +
                             "' for 'time style'\nValid arguments are:\n"
                             "  - 'full-iso'\n"
                             "  - 'long-iso'\n"
                             "  - 'iso'\n"
                             "  - 'locale'");
    }
    cfg.time_style = std::string(style);
  }

  // [GNU] du.c: the --inodes warning, printed once.
  if (cfg.show_inodes && cfg.apparent_size) {
    safeErrorPrint(
        "du: warning: options --apparent-size and -b are ineffective "
        "with --inodes\n");
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

// Counts the trailing separator run of |path| ('/' or '\\').
auto trailing_separator_run(const std::wstring& path) -> size_t {
  size_t run = 0;
  while (run < path.size()) {
    const wchar_t ch = path[path.size() - 1 - run];
    if (ch != L'/' && ch != L'\\') {
      break;
    }
    ++run;
  }
  return run;
}

// [GNU] Operand trailing separators collapse to a single slash (GitHub
// #1159): traversal and child entries build onto the stripped base so no
// doubled separator is ever printed, while the operand line keeps exactly
// one trailing slash when the operand had one (`du src/` prints `src/` and
// `src/bin`; `du src//` prints the same; `du src` prints `src`).
auto operand_base_path(const std::wstring& path) -> std::wstring {
  const size_t run = trailing_separator_run(path);
  if (run == 0) {
    return path;
  }
  if (run == path.size()) {
    return L"/";  // operand made only of separators resolves to the root
  }
  return path.substr(0, path.size() - run);
}

auto operand_display_path(const std::wstring& path) -> std::wstring {
  const size_t run = trailing_separator_run(path);
  if (run == 0) {
    return path;
  }
  if (run == path.size()) {
    return L"/";
  }
  return path.substr(0, path.size() - run) + L"/";
}

// Appends a directory entry name without doubling the separator when the
// directory already ends with one (e.g. the root operand "/").
auto append_entry_name(const std::wstring& dir, const wchar_t* name)
    -> std::wstring {
  if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') {
    return dir + L"\\" + name;
  }
  return dir + name;
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
// [GNU] -x compares file systems, and a file system here is a
// VOLUME (serial number), not a drive-letter spelling (du.c's
// devlist/FTS cross-device rule): a bind or subst alias of the same
// volume is the same file system, a different volume mounted under
// the same letter is not. Returns 0 when unknown.
auto volume_serial_of(const std::wstring& path) -> uint32_t {
  HANDLE hFile = CreateFileW(
      path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return 0;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  uint32_t serial = 0;
  if (GetFileInformationByHandle(hFile, &info)) {
    serial = info.dwVolumeSerialNumber;
  }
  CloseHandle(hFile);
  return serial;
}

// Identity of the link ITSELF (the reparse point is opened, not
// followed), for hard-link-style dedup of symlink encounters.
auto get_link_inode_key(const std::wstring& path) -> std::wstring {
  HANDLE hFile = CreateFileW(
      path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
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

// A symlink's own contribution: st_size of the link for apparent
// size, its own allocation (size rounded up to the volume cluster,
// mirroring get_file_size's model) otherwise.
auto link_entry_size(const WIN32_FIND_DATAW& data, const std::wstring& path,
                     bool apparent) -> uint64_t {
  const uint64_t size =
      (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
  if (apparent || size == 0) {
    return size;
  }
  const uint64_t cluster = get_volume_cluster_size(path);
  if (cluster == 0) {
    return size;
  }
  return (size + cluster - 1) / cluster * cluster;
}

auto calculate_dir_size(const std::wstring& path,
                        std::unordered_map<std::wstring, uint64_t>& sizes,
                        std::unordered_map<std::wstring, FILETIME>& times,
                        int current_depth, const DuConfig& cfg,
                        std::unordered_set<std::wstring>& seen_inodes,
                        std::unordered_set<std::wstring>& visited_dirs,
                        uint32_t root_volume = 0,
                        // [GNU] Entries are printed in traversal post-order:
                        // a directory line follows its subtree, so the operand
                        // directory prints last (Savannah #13956).
                        std::vector<std::wstring>* print_order = nullptr,
                        bool* had_error = nullptr)
    -> UsageSummary {
  WIN32_FIND_DATAW find_data;
  std::wstring search_path = append_entry_name(path, L"*");
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    // [GNU] An unreadable directory is diagnosed and fails the run
    // (du.c process_file error path); an empty one fails the search
    // with ERROR_FILE_NOT_FOUND and stays silent. This used to
    // swallow both cases.
    const DWORD find_error = GetLastError();
    if (find_error == ERROR_ACCESS_DENIED && had_error != nullptr) {
      std::string display = wstring_to_utf8(path);
      std::replace(display.begin(), display.end(), '\\', '/');
      safeErrorPrint("du: cannot read directory '");
      safeErrorPrint(display);
      safeErrorPrint("': ");
      safeErrorPrint(winux::win32_error::win32_posix_error_text(find_error));
      safeErrorPrint("\n");
      *had_error = true;
    }
    return {};
  }

  UsageSummary summary;
  // [GNU] --inodes: a directory counts ITSELF as one inode before its
  // entries are added (#1372). The count travels through the same
  // summary.size plumbing as byte totals, but byte sizes must never be
  // added into it (see the file branch below).
  if (cfg.show_inodes) {
    summary.size += 1;
    summary.full_size += 1;
  }
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

    std::wstring full_path = append_entry_name(path, find_data.cFileName);

    if (should_exclude(cfg, full_path, filename)) {
      continue;
    }

    const int child_depth = current_depth + 1;

    // [GNU] DU-1: under the default (-P) policy — and under -H,
    // which dereferences only operands — a symlink encountered in
    // traversal contributes ITSELF: its own size, its own inode for
    // dedup, its own times, and a directory symlink is NOT entered.
    // cfg.no_dereference was parsed but never read, and traversal
    // followed every link, so the default behavior was -L's.
    if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        !cfg.dereference) {
      uint64_t link_size =
          link_entry_size(find_data, full_path, cfg.apparent_size);
      bool counted = true;
      if (!cfg.count_links) {
        const std::wstring inode_key = get_link_inode_key(full_path);
        if (!inode_key.empty() && !seen_inodes.insert(inode_key).second) {
          counted = false;
          link_size = 0;
        }
      }
      const uint64_t entry_count = cfg.show_inodes ? (counted ? 1 : 0)
                                                   : link_size;
      summary.size += entry_count;
      summary.full_size += entry_count;
      if (cfg.show_time) {
        WIN32_FILE_ATTRIBUTE_DATA link_data{};
        link_data.ftCreationTime = find_data.ftCreationTime;
        link_data.ftLastAccessTime = find_data.ftLastAccessTime;
        link_data.ftLastWriteTime = find_data.ftLastWriteTime;
        const FILETIME link_time =
            get_selected_time(link_data, cfg.time_mode);
        update_latest_time(summary, link_time);
        if (cfg.count_all && counted &&
            (cfg.max_depth < 0 || child_depth <= cfg.max_depth)) {
          times[full_path] = link_time;
        }
      }
      if (counted && cfg.count_all &&
          (cfg.max_depth < 0 || child_depth <= cfg.max_depth)) {
        sizes[full_path] = entry_count;
        if (print_order != nullptr) {
          print_order->push_back(full_path);
        }
      }
      continue;
    }

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // [GNU] -x: a directory on a different VOLUME than the
      // operand root is skipped entirely (not counted, not
      // entered, not printed) — by serial, not drive letter.
      if (cfg.one_file_system && root_volume != 0) {
        const uint32_t child_volume = volume_serial_of(full_path);
        if (child_volume != 0 && child_volume != root_volume) {
          continue;
        }
      }
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
      UsageSummary child_summary = calculate_dir_size(
          full_path, sizes, times, child_depth, cfg, seen_inodes,
          visited_dirs, root_volume, print_order, had_error);
      if (!cfg.separate_dirs) {
        summary.size += child_summary.size;
      }
      summary.full_size += child_summary.full_size;
      if (child_summary.has_time) {
        update_latest_time(summary, child_summary.latest_time);
      }
    } else {
      // It's a file
      uint64_t file_size = get_file_size(full_path, cfg.apparent_size);
      // [GNU] Hard-linked files are counted once per invocation; later
      // occurrences of the same inode contribute nothing (uutils #9202
      // #10241 #10312 #9871).  Every counted file registers its inode,
      // not only multiply-linked ones: a later operand naming the same
      // file is a repeat encounter too (#1351).  --count-links opts out
      // of the dedup: every encounter then counts once AT FACE VALUE —
      // it never scales the size by the link count.
      bool counted = true;
      if (!cfg.count_links) {
        const std::wstring inode_key = get_inode_key(full_path);
        if (!inode_key.empty()) {
          if (!seen_inodes.insert(inode_key).second) {
            counted = false;
            file_size = 0;
          }
        }
      }
      // [GNU] --inodes counts entries instead of blocks (#1372): in
      // inode mode the file contributes exactly one per encounter —
      // never its byte size, and never its link count under
      // --count-links (#1351); that flag only disables the dedup above.
      // An encounter already deduplicated away contributes 0.  The
      // byte/block size contributes NOTHING in inode mode — the old
      // code added both, reporting e.g. 4097 for a directory holding
      // one file.
      uint64_t entry_count = file_size;
      if (cfg.show_inodes) {
        entry_count = counted ? 1 : 0;
        summary.size += entry_count;
      } else {
        summary.size += file_size;
      }
      summary.full_size += entry_count;
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

      // Count individual files if requested. In --inodes mode the
      // per-entry record is the entry count, not the byte size (#1372).
      if (counted && cfg.count_all &&
          (cfg.max_depth < 0 || child_depth <= cfg.max_depth)) {
        sizes[full_path] = entry_count;
        if (print_order != nullptr) {
          print_order->push_back(full_path);
        }
      }
    }
  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  // [GNU] A directory's OWN time always enters its tmax (du.c
  // process_file): the newest entry in the subtree wins, but the
  // directory itself is a candidate too. The old code consulted it
  // only when no descendant had a time at all.
  if (cfg.show_time && have_path_data) {
    update_latest_time(summary, get_selected_time(path_data, cfg.time_mode));
  }

  if (cfg.max_depth < 0 || current_depth <= cfg.max_depth) {
    sizes[path] = summary.size;
    if (print_order != nullptr) {
      print_order->push_back(path);
    }
    if (cfg.show_time && summary.has_time) {
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

  bool files0_names_ok = true;
  const std::string files0_from = ctx.get<std::string>("--files0-from", "");
  if (!files0_from.empty()) {
    if (!ctx.positionals.empty()) {
      // [GNU] du.c:1037-1043: the extra operand is named, then the
      // combination rule is stated, then usage.
      return std::unexpected(
          "extra operand '" + std::string(ctx.positionals.front()) +
          "'\nfile operands cannot be combined with --files0-from");
    }
    auto file_list = read_files0_from(files0_from, &files0_names_ok);
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
    // [GNU] A trailing separator run on the operand collapses to a single
    // slash: children build onto the stripped base (`src/` -> `src/bin`,
    // never `src//bin`) and the operand line keeps one trailing slash
    // (GitHub #1159).
    const std::wstring raw_path = utf8_to_wstring(path);
    // [L2 self-parse] POSIX drive spellings (`/d/src`, `/cygdrive/d/src`)
    // and the /dev pseudo-paths fold to their Win32 form at the shared
    // operand boundary — the same layer wc/grep/head normalize through —
    // so `/d/...` names the same file as `D:/...` however argv reached us
    // (#1174). Operand text and child entries both use the resolved form,
    // which is also the spelling the MSYS/Git-Bash runtime substitutes
    // before GNU du would see the argument.
    std::wstring resolved_path =
        native_path::make_api_path_operand_w(raw_path).normalized;
    // The raw operand's trailing separator run still decides the collapsed
    // single slash on the operand line (#1159); make_api_path_operand_w
    // re-adds a separator only when the target is a non-directory (#1052),
    // so restore the collapsed form whenever the fold dropped it.
    if (trailing_separator_run(raw_path) > 0 && !resolved_path.empty() &&
        !native_path::is_separator(resolved_path.back())) {
      resolved_path.push_back(L'\\');
    }
    const std::wstring wpath = operand_base_path(resolved_path);
    // Forward-slash display for the resolved operand text, exactly like the
    // child entries below (Savannah #13956 prints operand paths and their
    // descendants with forward slashes).
    const std::string operand_display =
        display_path(operand_display_path(resolved_path));

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

    // [GNU] DU-1 (operand half): GetFileAttributesW follows links,
    // so a symlink-to-directory operand looks like a directory here.
    // Under the default policy (and -H without -D semantics for
    // non-arg entries aside, operands need -H/-D or -L to be
    // followed) the operand is the LINK itself: one entry, counted
    // like a file operand below.
    bool operand_is_unfollowed_link = false;
    {
      std::error_code lec;
      const auto lst =
          std::filesystem::symlink_status(std::filesystem::path(wpath), lec);
      operand_is_unfollowed_link =
          !lec && std::filesystem::is_symlink(lst) && !cfg.dereference &&
          !cfg.dereference_args;
    }

    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) && !operand_is_unfollowed_link) {
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
      // Calculate directory size (-x anchors on the operand volume)
      const uint32_t root_volume =
          cfg.one_file_system ? volume_serial_of(wpath) : 0;
      bool operand_error = false;
      UsageSummary dir_summary = calculate_dir_size(
          wpath, sizes, times, 0, cfg, seen_inodes, visited_dirs,
          root_volume, &order, &operand_error);
      if (operand_error) {
        all_ok = false;
      }

      // Print directory size. [GNU] -c accumulates the FULL total:
      // -S changes printed lines, never what -c sums (DU-3).
      uint64_t dir_size = sizes[wpath];
      grand_total += dir_summary.full_size;

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
        if (is_operand_root) {
          // [GNU] The operand line echoes the operand with its trailing
          // separator run collapsed to one slash (`src/` stays `src/`,
          // `src//` also prints `src/`).
          safePrint(operand_display);
        } else {
          safePrint(display_path(entry_path));
        }
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
      // It's a file (or an unfollowed symlink operand, DU-1).
      uint64_t file_size = 0;
      if (operand_is_unfollowed_link) {
        WIN32_FIND_DATAW link_data{};
        const HANDLE hFind =
            FindFirstFileW(wpath.c_str(), &link_data);
        if (hFind != INVALID_HANDLE_VALUE) {
          file_size = link_entry_size(link_data, wpath, cfg.apparent_size);
          FindClose(hFind);
        }
      } else {
        file_size = get_file_size(wpath, cfg.apparent_size);
      }
      // [GNU] The seen-inode table spans the whole invocation (#1351):
      // a file operand whose inode was already counted — by an earlier
      // operand or inside an earlier directory traversal — prints
      // nothing and adds nothing, unless --count-links is in effect.
      // --count-links counts each encounter once at face value; it
      // never scales the size by the link count.
      if (!cfg.count_links) {
        const std::wstring inode_key = operand_is_unfollowed_link
                                           ? get_link_inode_key(wpath)
                                           : get_inode_key(wpath);
        if (!inode_key.empty() && !seen_inodes.insert(inode_key).second) {
          continue;
        }
      }
      // [GNU] --inodes: a file operand contributes exactly one inode to
      // the grand total, never its byte size (#1372) and never its link
      // count under --count-links (#1351) — that flag only disables the
      // dedup above.
      const uint64_t operand_total = cfg.show_inodes ? 1 : file_size;
      grand_total += operand_total;

      if (passes_threshold(cfg, operand_total)) {
        UsageSummary file_summary;
        if (cfg.show_time) {
          WIN32_FILE_ATTRIBUTE_DATA file_data{};
          bool have_data = false;
          if (operand_is_unfollowed_link) {
            WIN32_FIND_DATAW link_data{};
            const HANDLE hFind = FindFirstFileW(wpath.c_str(), &link_data);
            if (hFind != INVALID_HANDLE_VALUE) {
              file_data.ftCreationTime = link_data.ftCreationTime;
              file_data.ftLastAccessTime = link_data.ftLastAccessTime;
              file_data.ftLastWriteTime = link_data.ftLastWriteTime;
              have_data = true;
              FindClose(hFind);
            }
          } else if (GetFileAttributesExW(wpath.c_str(),
                                          GetFileExInfoStandard,
                                          &file_data)) {
            have_data = true;
          }
          if (have_data) {
            file_summary.latest_time =
                get_selected_time(file_data, cfg.time_mode);
            file_summary.has_time = true;
          }
        }
        safePrint(L"");
        if (cfg.show_inodes) {
          safePrint(std::to_string(operand_total));
        } else {
          print_scaled_size(file_size, cfg.output);
        }
        print_time_if_requested(file_summary, cfg);
        safePrint("\t");
        safePrint(operand_display);
        print_record_terminator(cfg.null_terminated);
      }
    }
  }

  // [GNU] The total line is unconditional (du.c:1135); --threshold
  // filters per-entry lines only. It used to vanish whenever the
  // grand total itself missed the threshold.
  if (cfg.total) {
    safePrint(L"");
    if (cfg.show_inodes) {
      safePrint(std::to_string(grand_total));
    } else {
      print_scaled_size(grand_total, cfg.output);
    }
    safePrint("\ttotal");
    print_record_terminator(cfg.null_terminated);
  }

  return all_ok && files0_names_ok;
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
