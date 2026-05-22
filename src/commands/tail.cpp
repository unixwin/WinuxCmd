/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: tail.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

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
 * @brief TAIL command options definition
 *
 * This array defines all the options supported by the tail command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -c, @a --bytes: Output the last NUM bytes; or use -c +NUM to output
 *   starting with byte NUM of each file [IMPLEMENTED]
 * - @a -f, @a --follow: Output appended data as the file grows [IMPLEMENTED]
 *
 * - @a -F: Same as --follow=name --retry [IMPLEMENTED]
 * - @a -n, @a --lines:
 * Output the last NUM lines, instead of the last 10; or
 * use -n +NUM to skip
 * NUM-1 lines at the start [IMPLEMENTED]
 * - @a -NUM: Obsolete GNU-compatible
 * shorthand for -n NUM [IMPLEMENTED]
 * - @a +NUM: Obsolete compatibility
 * shorthand for -n +NUM [IMPLEMENTED]
 * - @a
 * --max-unchanged-stats: With --follow=name, reopen a FILE which has not
 * changed
 *   size after N iterations to see if it has been renamed [IMPLEMENTED]
 * - @a --pid: With -f, terminate after process ID, PID dies [IMPLEMENTED]
 * -
 @a --debug: Output follow implementation details to stderr [IMPLEMENTED]
 * -
 * @a -q, @a --quiet: Never output headers giving file names [IMPLEMENTED]
 * - @a --silent: Never output headers giving file names [IMPLEMENTED]
 * - @a --retry: Keep trying to open a file if it is inaccessible [IMPLEMENTED]

 * * - @a -s, @a --sleep-interval: With -f, sleep for approximately N seconds
 *
 * between iterations [IMPLEMENTED]
 * - @a -v, @a --verbose: Always output
 * headers giving file names [IMPLEMENTED]
 * - @a -z, @a --zero-terminated: Line delimiter is NUL, not newline
 * [IMPLEMENTED]
 */
auto constexpr TAIL_OPTIONS = std::array{
    OPTION("-c", "--bytes",
           "output the last NUM bytes; or use -c +NUM to output\n"
           "starting with byte NUM of each file",
           STRING_TYPE),
    OPTION("", "--debug", "output extra follow diagnostics to stderr"),
    OPTION("-f", "", "output appended data as the file grows"),
    OPTION("", "--follow",
           "output appended data as the file grows; with --follow=name,\n"
           "follow the file name rather than the descriptor",
           OPTIONAL_STRING_TYPE),
    OPTION("-F", "", "same as --follow=name --retry"),
    OPTION("-n", "--lines",
           "output the last NUM lines, instead of the last 10; or\n"
           "use -n +NUM to skip NUM-1 lines at the start",
           STRING_TYPE),
    OPTION("", "--max-unchanged-stats",
           "with --follow=name, reopen a FILE which has not changed\n"
           "size after N iterations to see if it has been renamed\n"
           "[IMPLEMENTED]",
           INT_TYPE),
    OPTION("", "--pid",
           "with -f, terminate after process ID, PID dies [IMPLEMENTED]",
           INT_TYPE),
    OPTION("-q", "--quiet", "never output headers giving file names"),
    OPTION("", "--silent", "never output headers giving file names"),
    OPTION("", "--retry", "keep trying to open a file if it is inaccessible"),
    OPTION("-s", "--sleep-interval",
           "with -f, sleep for approximately N seconds between iterations",
           STRING_TYPE),
    OPTION("-v", "--verbose", "always output headers giving file names"),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

namespace tail_pipeline {
namespace cp = core::pipeline;

struct CountSpec {
  std::uintmax_t value = 10;
  bool from_start = false;
};

struct TailConfig {
  bool by_bytes = false;
  CountSpec spec;
  bool quiet = false;
  bool verbose = false;
  bool follow = false;
  bool follow_by_name = false;
  bool retry = false;
  bool debug = false;
  std::vector<DWORD> follow_pids;
  bool stdin_mode = false;
  char delimiter = '\n';
  std::chrono::milliseconds sleep_interval{1000};
  std::uintmax_t max_unchanged_stats = 5;
};

auto option_matches(const OptionMeta& meta, std::string_view short_name,
                    std::string_view long_name) -> bool {
  return (!short_name.empty() && meta.short_name == short_name) ||
         (!long_name.empty() && meta.long_name == long_name);
}

auto stream_all(std::istream& in) -> void {
  std::array<char, 8192> buffer{};
  while (in.good()) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto got = in.gcount();
    if (got <= 0) break;
    safePrint(std::string_view(buffer.data(), static_cast<size_t>(got)));
  }
}

struct CountSuffix {
  std::string_view suffix;
  std::uintmax_t base;
  unsigned power;
};

auto checked_pow(std::uintmax_t base, unsigned power)
    -> std::optional<std::uintmax_t> {
  std::uintmax_t result = 1;
  for (unsigned i = 0; i < power; ++i) {
    if (result > std::numeric_limits<std::uintmax_t>::max() / base) {
      return std::nullopt;
    }
    result *= base;
  }
  return result;
}

auto apply_suffix_multiplier(std::uintmax_t value, std::string_view suffix)
    -> std::optional<std::uintmax_t> {
  static constexpr std::array suffixes{
      CountSuffix{"", 1, 0},       CountSuffix{"b", 512, 1},
      CountSuffix{"K", 1024, 1},   CountSuffix{"KB", 1000, 1},
      CountSuffix{"KiB", 1024, 1}, CountSuffix{"M", 1024, 2},
      CountSuffix{"MB", 1000, 2},  CountSuffix{"MiB", 1024, 2},
      CountSuffix{"G", 1024, 3},   CountSuffix{"GB", 1000, 3},
      CountSuffix{"GiB", 1024, 3}, CountSuffix{"T", 1024, 4},
      CountSuffix{"TB", 1000, 4},  CountSuffix{"TiB", 1024, 4},
      CountSuffix{"P", 1024, 5},   CountSuffix{"PB", 1000, 5},
      CountSuffix{"PiB", 1024, 5}, CountSuffix{"E", 1024, 6},
      CountSuffix{"EB", 1000, 6},  CountSuffix{"EiB", 1024, 6},
      CountSuffix{"Z", 1024, 7},   CountSuffix{"ZB", 1000, 7},
      CountSuffix{"ZiB", 1024, 7}, CountSuffix{"Y", 1024, 8},
      CountSuffix{"YB", 1000, 8},  CountSuffix{"YiB", 1024, 8},
      CountSuffix{"R", 1024, 9},   CountSuffix{"RB", 1000, 9},
      CountSuffix{"RiB", 1024, 9}, CountSuffix{"Q", 1024, 10},
      CountSuffix{"QB", 1000, 10}, CountSuffix{"QiB", 1024, 10}};

  for (const auto& entry : suffixes) {
    if (entry.suffix != suffix) continue;
    auto multiplier = checked_pow(entry.base, entry.power);
    if (!multiplier)
      return value == 0 ? std::optional<std::uintmax_t>{0} : std::nullopt;
    if (value > std::numeric_limits<std::uintmax_t>::max() / *multiplier) {
      return std::nullopt;
    }
    return value * *multiplier;
  }

  return std::nullopt;
}

auto parse_numeric_with_suffix(std::string_view text)
    -> std::optional<std::uintmax_t> {
  if (text.empty()) return std::nullopt;

  size_t i = 0;
  while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
    ++i;
  }
  if (i == 0) return std::nullopt;

  std::uintmax_t base = 0;
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + i, base);
  if (ec != std::errc() || ptr != text.data() + i) return std::nullopt;

  return apply_suffix_multiplier(base, text.substr(i));
}

auto parse_count_spec(std::string spec_text, std::string_view opt_name)
    -> cp::Result<CountSpec> {
  if (spec_text.empty()) {
    return std::unexpected("invalid number of " + std::string(opt_name));
  }

  CountSpec spec;
  if (spec_text[0] == '+') {
    spec.from_start = true;
    spec_text = spec_text.substr(1);  // Avoid modifying original string
  } else if (spec_text[0] == '-') {
    spec_text = spec_text.substr(1);
  }

  if (spec_text.empty()) {
    return std::unexpected("invalid number of " + std::string(opt_name));
  }

  auto parsed = parse_numeric_with_suffix(spec_text);
  if (!parsed.has_value()) {
    return std::unexpected("invalid number of " + std::string(opt_name));
  }

  spec.value = *parsed;
  return spec;
}

auto parse_sleep_interval(std::string_view text)
    -> std::optional<std::chrono::milliseconds> {
  if (text.empty()) return std::nullopt;
  std::string s(text);
  char* end = nullptr;
  errno = 0;
  double seconds = std::strtod(s.c_str(), &end);
  if (errno != 0 || end != s.c_str() + s.size() || seconds < 0.0) {
    return std::nullopt;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::duration<double>(seconds));
}

auto process_is_alive(DWORD pid) -> bool {
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (!process) return false;
  DWORD wait_rc = WaitForSingleObject(process, 0);
  CloseHandle(process);
  return wait_rc == WAIT_TIMEOUT;
}

auto all_follow_pids_dead(const TailConfig& config) -> bool {
  if (config.follow_pids.empty()) return false;
  return std::ranges::none_of(config.follow_pids, process_is_alive);
}

auto should_stop_follow(const TailConfig& config) -> bool {
  if (all_follow_pids_dead(config)) return true;
  return (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
         (GetAsyncKeyState('C') & 0x8000);
}

struct FileIdentity {
  DWORD volume_serial = 0;
  std::uint64_t file_index = 0;
};

struct FileStatus {
  FileIdentity identity;
  std::uintmax_t size = 0;
};

auto same_identity(const FileIdentity& lhs, const FileIdentity& rhs) -> bool {
  return lhs.volume_serial == rhs.volume_serial &&
         lhs.file_index == rhs.file_index;
}

auto read_file_status(const std::string& file) -> std::optional<FileStatus> {
  auto wfile = utf8_to_wstring(file);
  HANDLE handle =
      CreateFileW(wfile.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

  BY_HANDLE_FILE_INFORMATION info{};
  LARGE_INTEGER size{};
  std::optional<FileStatus> status;
  if (GetFileInformationByHandle(handle, &info) &&
      GetFileSizeEx(handle, &size)) {
    status = FileStatus{
        .identity =
            FileIdentity{
                .volume_serial = info.dwVolumeSerialNumber,
                .file_index =
                    static_cast<std::uint64_t>(info.nFileIndexLow) |
                    (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32)},
        .size = static_cast<std::uintmax_t>(size.QuadPart)};
  }
  CloseHandle(handle);
  return status;
}

auto read_file_identity(const std::string& file)
    -> std::optional<FileIdentity> {
  auto status = read_file_status(file);
  if (!status) return std::nullopt;
  return status->identity;
}

auto streampos_to_size(std::streampos pos) -> std::uintmax_t {
  if (pos == std::streampos(-1)) return 0;
  auto offset = static_cast<std::streamoff>(pos);
  if (offset <= 0) return 0;
  return static_cast<std::uintmax_t>(offset);
}

auto output_new_data(std::ifstream& input, std::streampos& offset,
                     std::string_view header = {}) -> bool {
  input.clear();
  input.seekg(0, std::ios::end);
  auto end = input.tellg();
  if (end == std::streampos(-1)) return !input.bad();

  if (end < offset) offset = 0;
  if (end == offset) return true;

  input.clear();
  input.seekg(offset);
  auto remaining = end - offset;
  std::array<char, 8192> buffer{};
  bool header_printed = false;
  while (remaining > 0 && input.good()) {
    auto chunk = std::min<std::streamoff>(
        remaining, static_cast<std::streamoff>(buffer.size()));
    input.read(buffer.data(), static_cast<std::streamsize>(chunk));
    auto got = input.gcount();
    if (got <= 0) break;
    if (!header.empty() && !header_printed) {
      safePrint(header);
      header_printed = true;
    }
    safePrint(std::string_view(buffer.data(), static_cast<size_t>(got)));
    remaining -= got;
  }

  offset = end;
  return !input.bad();
}

auto output_tail(std::istream& in, const TailConfig& config) -> void {
  if (config.by_bytes) {
    size_t n = static_cast<size_t>(config.spec.value);
    if (config.spec.from_start) {
      size_t skip = n > 0 ? n - 1 : 0;
      std::array<char, 8192> discard{};
      while (skip > 0 && in.good()) {
        size_t chunk = std::min(skip, discard.size());
        in.read(discard.data(), static_cast<std::streamsize>(chunk));
        auto got = in.gcount();
        if (got <= 0) return;
        skip -= static_cast<size_t>(got);
      }
      stream_all(in);
      return;
    }

    if (n == 0) return;
    std::deque<char> trailing;
    std::array<char, 8192> buffer{};
    while (in.good()) {
      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      auto got = in.gcount();
      if (got <= 0) break;
      for (std::streamsize i = 0; i < got; ++i) {
        trailing.push_back(buffer[static_cast<size_t>(i)]);
        if (trailing.size() > n) trailing.pop_front();
      }
    }
    if (!trailing.empty()) {
      std::string out;
      out.reserve(trailing.size());
      for (char ch : trailing) out.push_back(ch);
      safePrint(out);
    }
    return;
  }

  size_t n = static_cast<size_t>(config.spec.value);
  if (config.spec.from_start) {
    size_t start = n > 0 ? n - 1 : 0;
    if (start == 0) {
      stream_all(in);
      return;
    }

    size_t record_index = 0;
    std::string current;
    char ch = '\0';
    while (in.get(ch)) {
      current.push_back(ch);
      if (ch == config.delimiter) {
        if (record_index >= start) safePrint(current);
        current.clear();
        ++record_index;
      }
    }
    if (!current.empty() && record_index >= start) {
      safePrint(current);
    }
    return;
  }

  if (n == 0) return;
  std::deque<std::string> trailing_records;
  std::string current;
  char ch = '\0';
  while (in.get(ch)) {
    current.push_back(ch);
    if (ch == config.delimiter) {
      trailing_records.push_back(std::move(current));
      current.clear();
      if (trailing_records.size() > n) trailing_records.pop_front();
    }
  }
  if (!current.empty()) {
    trailing_records.push_back(std::move(current));
    if (trailing_records.size() > n) trailing_records.pop_front();
  }
  for (const auto& rec : trailing_records) safePrint(rec);
}

template <size_t N>
auto check_unsupported(const CommandContext<N>&) -> cp::Result<void> {
  return {};
}

template <size_t N>
auto build_config(const CommandContext<N>& ctx) -> cp::Result<TailConfig> {
  TailConfig config;
  config.delimiter = ctx.get<bool>("--zero-terminated", false) ? '\0' : '\n';
  config.debug = ctx.get<bool>("--debug", false);
  config.follow_by_name = ctx.get<bool>("-F", false);
  if (ctx.has("--follow")) {
    std::string follow_mode = ctx.get<std::string>("--follow", "");
    if (follow_mode == "name") {
      config.follow_by_name = true;
    } else if (!follow_mode.empty() && follow_mode != "descriptor") {
      return std::unexpected("invalid follow mode");
    }
  }
  config.follow = ctx.get<bool>("-f", false) || ctx.has("--follow") ||
                  config.follow_by_name;
  config.retry = ctx.get<bool>("--retry", false) || config.follow_by_name;
  for (int pid : ctx.template get_all<int>("--pid")) {
    if (pid < 0) return std::unexpected("invalid process ID");
    config.follow_pids.push_back(static_cast<DWORD>(pid));
  }

  if (ctx.has("--max-unchanged-stats")) {
    int max_unchanged_stats = ctx.get<int>("--max-unchanged-stats", -1);
    if (max_unchanged_stats < 0) {
      return std::unexpected("invalid max unchanged stats");
    }
    config.max_unchanged_stats =
        static_cast<std::uintmax_t>(max_unchanged_stats);
  }

  std::string sleep_arg = ctx.get<std::string>("--sleep-interval", "");
  if (sleep_arg.empty()) sleep_arg = ctx.get<std::string>("-s", "");
  if (!sleep_arg.empty()) {
    auto parsed_sleep = parse_sleep_interval(sleep_arg);
    if (!parsed_sleep) {
      return std::unexpected("invalid sleep interval");
    }
    config.sleep_interval = *parsed_sleep;
  }

  auto unsupported = check_unsupported(ctx);
  if (!unsupported) return std::unexpected(unsupported.error());

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= N) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    if (option_matches(meta, "-q", "--quiet") ||
        option_matches(meta, "", "--silent")) {
      config.quiet = true;
      config.verbose = false;
      continue;
    }
    if (option_matches(meta, "-v", "--verbose")) {
      config.verbose = true;
      config.quiet = false;
      continue;
    }

    auto value = std::get_if<std::string>(&occurrence.value);
    if (!value) continue;

    if (option_matches(meta, "-c", "--bytes")) {
      auto spec = parse_count_spec(*value, "bytes");
      if (!spec) return std::unexpected(spec.error());
      config.by_bytes = true;
      config.spec = *spec;
      continue;
    }
    if (option_matches(meta, "-n", "--lines")) {
      auto spec = parse_count_spec(*value, "lines");
      if (!spec) return std::unexpected(spec.error());
      config.by_bytes = false;
      config.spec = *spec;
      continue;
    }
  }

  return config;
}

auto open_file_with_retry(const std::string& file, const TailConfig& config)
    -> std::optional<std::ifstream> {
  while (true) {
    std::ifstream input(file, std::ios::binary);
    if (input.is_open()) return input;
    if (!config.retry || should_stop_follow(config)) return std::nullopt;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

auto follow_descriptor(const std::string& file, const TailConfig& config)
    -> bool {
  std::ifstream monitor_file(file, std::ios::binary);
  if (!monitor_file.is_open()) {
    safeErrorPrint("tail: cannot open '");
    safeErrorPrint(file);
    safeErrorPrint("' for following\n");
    return false;
  }

  monitor_file.seekg(0, std::ios::end);
  auto offset = monitor_file.tellg();
  if (offset == std::streampos(-1)) offset = 0;

  while (true) {
    if (should_stop_follow(config)) break;
    std::this_thread::sleep_for(config.sleep_interval);
    if (!output_new_data(monitor_file, offset)) {
      safeErrorPrint("tail: error reading '");
      safeErrorPrint(file);
      safeErrorPrint("' while following\n");
      return false;
    }
  }

  return true;
}

auto follow_name(const std::string& file, const TailConfig& config,
                 std::optional<FileIdentity> identity, std::streampos offset)
    -> bool {
  std::uintmax_t unchanged_stats = 0;

  while (true) {
    if (should_stop_follow(config)) break;
    std::this_thread::sleep_for(config.sleep_interval);

    auto current_status = read_file_status(file);
    if (!current_status) {
      if (config.retry) continue;
      safeErrorPrint("tail: cannot open '");
      safeErrorPrint(file);
      safeErrorPrint("' for following\n");
      return false;
    }

    bool identity_changed =
        !identity || !same_identity(*identity, current_status->identity);
    bool unchanged = current_status->size == streampos_to_size(offset);

    if (identity_changed) {
      if (unchanged_stats < config.max_unchanged_stats) {
        ++unchanged_stats;
        continue;
      }
      identity = current_status->identity;
      offset = 0;
      unchanged_stats = 0;
    } else if (unchanged) {
      if (unchanged_stats < config.max_unchanged_stats) {
        ++unchanged_stats;
      } else {
        unchanged_stats = 0;
      }
      continue;
    } else {
      unchanged_stats = 0;
    }

    std::ifstream current(file, std::ios::binary);
    if (!current.is_open()) {
      if (config.retry) continue;
      safeErrorPrint("tail: cannot open '");
      safeErrorPrint(file);
      safeErrorPrint("' for following\n");
      return false;
    }

    if (!output_new_data(current, offset)) {
      safeErrorPrint("tail: error reading '");
      safeErrorPrint(file);
      safeErrorPrint("' while following\n");
      return false;
    }
  }

  return true;
}

struct FollowTarget {
  std::string file;
  std::optional<FileIdentity> identity;
  std::streampos offset = 0;
  std::uintmax_t unchanged_stats = 0;
  std::ifstream descriptor;
  bool active = true;
};

auto follow_header(const FollowTarget& target, const TailConfig& config)
    -> std::string {
  std::string header(1, config.delimiter);
  header += "==> ";
  header += target.file;
  header += " <==";
  header.push_back(config.delimiter);
  return header;
}

auto debug_follow_start(const TailConfig& config, size_t file_count) -> void {
  if (!config.debug) return;
  safeErrorPrint("tail: using polling follow implementation\n");
  safeErrorPrint("tail: following by ");
  safeErrorPrint(config.follow_by_name ? "name" : "descriptor");
  safeErrorPrint(" for ");
  safeErrorPrint(std::to_string(file_count));
  safeErrorPrint(file_count == 1 ? " file\n" : " files\n");
  if (!config.follow_pids.empty()) {
    safeErrorPrint("tail: monitoring ");
    safeErrorPrint(std::to_string(config.follow_pids.size()));
    safeErrorPrint(config.follow_pids.size() == 1 ? " process ID\n"
                                                  : " process IDs\n");
  }
}

auto follow_descriptor_target(FollowTarget& target, const TailConfig& config,
                              bool multi) -> bool {
  if (!target.descriptor.is_open()) {
    target.descriptor.open(target.file, std::ios::binary);
    if (!target.descriptor.is_open()) {
      safeErrorPrint("tail: cannot open '");
      safeErrorPrint(target.file);
      safeErrorPrint("' for following\n");
      target.active = false;
      return false;
    }
    target.descriptor.seekg(0, std::ios::end);
    target.offset = target.descriptor.tellg();
    if (target.offset == std::streampos(-1)) target.offset = 0;
  }

  if (!output_new_data(target.descriptor, target.offset,
                       multi ? follow_header(target, config) : "")) {
    safeErrorPrint("tail: error reading '");
    safeErrorPrint(target.file);
    safeErrorPrint("' while following\n");
    target.active = false;
    return false;
  }
  return true;
}

auto follow_name_target(FollowTarget& target, const TailConfig& config,
                        bool multi) -> bool {
  auto current_status = read_file_status(target.file);
  if (!current_status) {
    if (config.retry) return true;
    safeErrorPrint("tail: cannot open '");
    safeErrorPrint(target.file);
    safeErrorPrint("' for following\n");
    target.active = false;
    return false;
  }

  bool identity_changed =
      !target.identity ||
      !same_identity(*target.identity, current_status->identity);
  bool unchanged = current_status->size == streampos_to_size(target.offset);

  if (identity_changed) {
    if (target.unchanged_stats < config.max_unchanged_stats) {
      ++target.unchanged_stats;
      return true;
    }
    target.identity = current_status->identity;
    target.offset = 0;
    target.unchanged_stats = 0;
  } else if (unchanged) {
    if (target.unchanged_stats < config.max_unchanged_stats) {
      ++target.unchanged_stats;
    } else {
      target.unchanged_stats = 0;
    }
    return true;
  } else {
    target.unchanged_stats = 0;
  }

  std::ifstream current(target.file, std::ios::binary);
  if (!current.is_open()) {
    if (config.retry) return true;
    safeErrorPrint("tail: cannot open '");
    safeErrorPrint(target.file);
    safeErrorPrint("' for following\n");
    target.active = false;
    return false;
  }

  if (!output_new_data(current, target.offset,
                       multi ? follow_header(target, config) : "")) {
    safeErrorPrint("tail: error reading '");
    safeErrorPrint(target.file);
    safeErrorPrint("' while following\n");
    target.active = false;
    return false;
  }
  return true;
}

auto follow_targets(std::vector<FollowTarget>& targets,
                    const TailConfig& config) -> bool {
  debug_follow_start(config, targets.size());
  bool ok = true;
  bool multi = targets.size() > 1 && !config.quiet;

  while (!targets.empty()) {
    if (should_stop_follow(config)) break;
    std::this_thread::sleep_for(config.sleep_interval);

    for (auto& target : targets) {
      if (!target.active) continue;
      bool target_ok = config.follow_by_name
                           ? follow_name_target(target, config, multi)
                           : follow_descriptor_target(target, config, multi);
      if (!target_ok) ok = false;
    }

    std::erase_if(targets,
                  [](const FollowTarget& target) { return !target.active; });
  }

  return ok;
}

}  // namespace tail_pipeline

REGISTER_COMMAND(
    tail, "tail", "tail [OPTION]... [FILE]...",
    "Print the last 10 lines of each FILE to standard output.\n"
    "With more than one FILE, precede each with a header giving the file "
    "name.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.",
    "  tail file.txt\n"
    "  tail -n 20 file.txt\n"
    "  tail -20 file.txt\n"
    "  tail -n +5 file.txt\n"
    "  tail +5 file.txt\n"
    "  tail -c 64 file.txt\n"
    "  tail -v a.txt b.txt",
    "head(1), cat(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TAIL_OPTIONS) {
  using namespace tail_pipeline;

  auto config_result = build_config(ctx);
  if (!config_result) {
    cp::report_error(config_result, L"tail");
    return 1;
  }
  auto config = *config_result;

  // Use SmallVector for files (max 64 files) - all stack-allocated
  SmallVector<std::string, 64> files{};
  for (auto p : ctx.positionals) {
    std::string file_arg(p);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    files.push_back(file_arg);
  }
  if (files.empty()) files.push_back("-");

  bool any_error = false;
  bool first_print = true;
  bool multi = files.size() > 1;
  std::vector<FollowTarget> follow_targets_to_run;

  for (size_t i = 0; i < files.size(); ++i) {
    const auto& file = files[i];

    bool show_header = config.verbose || (multi && !config.quiet);
    if (show_header) {
      if (!first_print) safePrint(std::string(1, config.delimiter));
      safePrint("==> ");
      safePrint(file == "-" ? "standard input" : file);
      safePrint(" <==");
      safePrint(std::string(1, config.delimiter));
    }

    if (file == "-") {
      config.stdin_mode = true;
      output_tail(std::cin, config);
      if (std::cin.bad()) {
        safeErrorPrint("tail: error reading '-'\n");
        any_error = true;
      }
    } else {
      auto input = open_file_with_retry(file, config);
      if (!input) {
        safeErrorPrint("tail: cannot open '");
        safeErrorPrint(file);
        safeErrorPrint("'\n");
        any_error = true;
        continue;
      }

      output_tail(*input, config);
      if (input->bad()) {
        safeErrorPrint("tail: error reading '");
        safeErrorPrint(file);
        safeErrorPrint("'\n");
        any_error = true;
      }

      if (config.follow) {
        FollowTarget target;
        target.file = file;
        target.identity = read_file_identity(file);
        input->clear();
        input->seekg(0, std::ios::end);
        target.offset = input->tellg();
        if (target.offset == std::streampos(-1)) target.offset = 0;
        follow_targets_to_run.push_back(std::move(target));
      }
    }

    first_print = false;
  }

  if (!follow_targets_to_run.empty() &&
      !follow_targets(follow_targets_to_run, config)) {
    any_error = true;
  }

  return any_error ? 1 : 0;
}
