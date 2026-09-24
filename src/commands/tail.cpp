// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "pch/pch.h"

// include other header after pch.h

#include "core/command_macros.h"

import std;

import core;

import utils;

import container;

using cmd::meta::option_matches;
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
    // [GNU] -c, --bytes
    OPTION("-c", "--bytes",
           "output the last NUM bytes; or use -c +NUM to output\n"
           "starting with byte NUM of each file",
           STRING_TYPE),
    // [GNU] --debug
    OPTION("", "--debug", "output extra follow diagnostics to stderr"),
    // [GNU] -f
    OPTION("-f", "", "output appended data as the file grows"),
    // [GNU] --follow
    OPTION("", "--follow",
           "output appended data as the file grows; with --follow=name,\n"
           "follow the file name rather than the descriptor",
           OPTIONAL_STRING_TYPE),
    // [GNU] -F
    OPTION("-F", "", "same as --follow=name --retry"),
    // [GNU] -n, --lines
    OPTION("-n", "--lines",
           "output the last NUM lines, instead of the last 10; or\n"
           "use -n +NUM to skip NUM-1 lines at the start",
           STRING_TYPE),
    // [GNU] --max-unchanged-stats
    OPTION("", "--max-unchanged-stats",
           "with --follow=name, reopen a FILE which has not changed\n"
           "size after N iterations to see if it has been renamed\n"
           "[IMPLEMENTED]",
           INT_TYPE),
    // [GNU] --pid
    OPTION("", "--pid",
           "with -f, terminate after process ID, PID dies [IMPLEMENTED]",
           INT_TYPE),
    // [GNU] -q, --quiet
    OPTION("-q", "--quiet", "never output headers giving file names"),
    // [GNU] --silent
    OPTION("", "--silent", "never output headers giving file names"),
    // [GNU] --retry
    OPTION("", "--retry", "keep trying to open a file if it is inaccessible"),
    // [GNU] --use-polling
    // [DIFFERS] - not applicable; Windows uses ReadDirectoryChangesW
    OPTION("", "--use-polling",
           "disable native directory change watching and use polling instead"),
    // [GNU] -s, --sleep-interval
    OPTION("-s", "--sleep-interval",
           "with -f, sleep for approximately N seconds between iterations",
           STRING_TYPE),
    // [GNU] -v, --verbose
    OPTION("-v", "--verbose", "always output headers giving file names"),
    // [GNU] -z, --zero-terminated
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
  bool explicit_retry = false;
  bool retry = false;
  bool debug = false;
  std::vector<DWORD> follow_pids;
  bool stdin_mode = false;
  char delimiter = '\n';
  std::chrono::milliseconds sleep_interval{1000};
  std::uintmax_t max_unchanged_stats = 5;
};

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

// Distinguishes malformed input from an overflowing value so the GNU
// EOVERFLOW diagnostic can be reproduced.
struct NumericParse {
  std::uintmax_t value = 0;
  bool ok = false;
  bool overflow = false;
};

auto parse_numeric_with_suffix_status(std::string_view text) -> NumericParse {
  if (text.empty()) return {};

  size_t i = 0;
  while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
    ++i;
  }
  if (i == 0) return {};

  std::uintmax_t base = 0;
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + i, base);
  if (ec == std::errc::result_out_of_range) {
    return {.value = 0, .ok = false, .overflow = true};
  }
  if (ec != std::errc() || ptr != text.data() + i) return {};

  auto scaled = apply_suffix_multiplier(base, text.substr(i));
  if (!scaled.has_value()) {
    return {.value = 0, .ok = false, .overflow = true};
  }
  return {.value = *scaled, .ok = true, .overflow = false};
}

auto parse_count_spec(std::string spec_text, std::string_view opt_name)
    -> cp::Result<CountSpec> {
  // GNU strips a leading '-' before parsing but keeps '+' in the
  // diagnostic string, and appends the EOVERFLOW message on overflow.
  std::string_view quoted = spec_text;
  if (!quoted.empty() && quoted[0] == '-') {
    quoted.remove_prefix(1);
  }
  auto make_error = [&](const bool overflow) -> cp::Error {
    auto message = "invalid number of " + std::string(opt_name) + ": '" +
                   std::string(quoted) + "'";
    if (overflow) {
      message += ": Value too large for defined data type";
    }
    return cp::Error(std::move(message));
  };

  if (spec_text.empty()) {
    return std::unexpected(make_error(false));
  }

  CountSpec spec;
  if (spec_text[0] == '+') {
    spec.from_start = true;
    spec_text = spec_text.substr(1);  // Avoid modifying original string
  } else if (spec_text[0] == '-') {
    spec_text = spec_text.substr(1);
  }

  if (spec_text.empty()) {
    return std::unexpected(make_error(false));
  }

  auto parsed = parse_numeric_with_suffix_status(spec_text);
  if (!parsed.ok) {
    return std::unexpected(make_error(parsed.overflow));
  }

  spec.value = parsed.value;
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
  // Extended API path: pseudo-devices and >MAX_PATH operands (#1061).
  const auto operand = native_path::make_api_path_operand(file);
  HANDLE handle =
      CreateFileW(operand.extended.c_str(), FILE_READ_ATTRIBUTES,
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

auto stream_needs_text_decoding(std::istream& in) -> bool {
  auto original = in.tellg();
  if (original == std::streampos(-1)) return true;

  std::array<char, 4096> sample{};
  in.read(sample.data(), static_cast<std::streamsize>(sample.size()));
  auto got = static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0));
  in.clear();
  in.seekg(original);

  if (got >= 3 && static_cast<std::uint8_t>(sample[0]) == 0xEF &&
      static_cast<std::uint8_t>(sample[1]) == 0xBB &&
      static_cast<std::uint8_t>(sample[2]) == 0xBF) {
    return true;
  }
  if (got >= 2 && ((static_cast<std::uint8_t>(sample[0]) == 0xFF &&
                    static_cast<std::uint8_t>(sample[1]) == 0xFE) ||
                   (static_cast<std::uint8_t>(sample[0]) == 0xFE &&
                    static_cast<std::uint8_t>(sample[1]) == 0xFF))) {
    return true;
  }

  return std::find(sample.begin(), sample.begin() + got, '\0') !=
         sample.begin() + got;
}

auto seek_to_end(std::ifstream& input) -> std::optional<std::uintmax_t> {
  input.clear();
  input.seekg(0, std::ios::end);
  auto end = input.tellg();
  if (end == std::streampos(-1) || input.bad()) return std::nullopt;
  return streampos_to_size(end);
}

auto output_file_range(std::ifstream& input, std::uintmax_t start,
                       std::optional<std::uintmax_t> byte_count = std::nullopt)
    -> bool {
  input.clear();
  input.seekg(static_cast<std::streamoff>(start), std::ios::beg);
  if (input.bad()) return false;

  std::array<char, 64 * 1024> buffer{};
  std::uintmax_t remaining =
      byte_count.value_or(std::numeric_limits<std::uintmax_t>::max());
  while (remaining > 0 && input.good()) {
    const auto want = static_cast<std::streamsize>(std::min<std::uintmax_t>(
        remaining, static_cast<std::uintmax_t>(buffer.size())));
    input.read(buffer.data(), want);
    auto got =
        static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    if (got == 0) break;
    safePrint(std::string_view(buffer.data(), got));
    if (byte_count.has_value()) remaining -= got;
  }

  return !input.bad();
}

auto output_tail_seekable_bytes(std::ifstream& input, const TailConfig& config)
    -> bool {
  if (!config.by_bytes) return false;
  auto end = seek_to_end(input);
  if (!end) return false;

  const std::uintmax_t n = config.spec.value;
  std::uintmax_t start = 0;
  if (config.spec.from_start) {
    start = n > 0 ? n - 1 : 0;
    if (start >= *end) return true;
  } else {
    if (n == 0) return true;
    start = n >= *end ? 0 : *end - n;
  }

  return output_file_range(input, start);
}

auto output_tail_seekable_lines(std::ifstream& input, const TailConfig& config)
    -> bool {
  if (config.by_bytes || config.spec.from_start) return false;
  std::uintmax_t lines = config.spec.value;
  if (lines == 0) return true;

  auto end = seek_to_end(input);
  if (!end) return false;
  if (*end == 0) return true;

  constexpr std::uintmax_t kChunkSize = 64 * 1024;
  std::vector<char> buffer(static_cast<size_t>(kChunkSize));
  std::uintmax_t pos = *end;
  bool checked_last_byte = false;

  while (pos > 0) {
    const std::uintmax_t chunk_start = pos > kChunkSize ? pos - kChunkSize : 0;
    const auto chunk_size = static_cast<size_t>(pos - chunk_start);
    input.clear();
    input.seekg(static_cast<std::streamoff>(chunk_start), std::ios::beg);
    input.read(buffer.data(), static_cast<std::streamsize>(chunk_size));
    auto got =
        static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    if (got == 0 || input.bad()) return false;

    if (!checked_last_byte) {
      checked_last_byte = true;
      if (buffer[got - 1] != config.delimiter && lines > 0) --lines;
    }

    for (size_t i = got; i > 0; --i) {
      if (buffer[i - 1] != config.delimiter) continue;
      if (lines == 0) {
        return output_file_range(input, chunk_start + i);
      }
      --lines;
    }

    pos = chunk_start;
  }

  return output_file_range(input, 0);
}

auto output_new_data(std::ifstream& input, std::streampos& offset,
                     std::string_view header = {},
                     std::string_view diag_name = {}) -> bool {
  input.clear();
  input.seekg(0, std::ios::end);
  auto end = input.tellg();
  if (end == std::streampos(-1)) return !input.bad();

  if (end < offset) {
    // [GNU] A shrinking file reports "tail: NAME: file truncated" once and
    // is then followed from the start.
    if (!diag_name.empty()) {
      safeErrorPrint("tail: ");
      safeErrorPrint(winux::i18n::format("command.tail.follow.file_truncated",
                                         "{}: file truncated", diag_name));
      safeErrorPrint("\n");
    }
    offset = 0;
  }
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

auto output_tail_from_start_records(std::istream& in, size_t records_to_skip,
                                    char delimiter) -> void {
  if (records_to_skip == 0) {
    stream_all(in);
    return;
  }

  std::array<char, 64 * 1024> buffer{};
  while (records_to_skip > 0 && in.good()) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto got = static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0));
    if (got == 0) break;

    for (size_t i = 0; i < got; ++i) {
      if (buffer[i] != delimiter) continue;
      if (--records_to_skip == 0) {
        const size_t remainder_start = i + 1;
        if (remainder_start < got) {
          safePrint(std::string_view(buffer.data() + remainder_start,
                                     got - remainder_start));
        }
        stream_all(in);
        return;
      }
    }
  }
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
    output_tail_from_start_records(in, n > 0 ? n - 1 : 0, config.delimiter);
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

auto open_input_file(const std::string& file) -> std::ifstream {
  return file_io::open_binary_file(file);
}

auto describe_open_failure(const std::string& file) -> std::string {
  std::wstring wfile = utf8_to_wstring(file);
  DWORD attrs = native_path::attributes_w(utf8_to_wstring(file));
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return "No such file or directory";
  }
  if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return "Is a directory";
  }
  return "Permission denied";
}

auto is_directory_open_failure(std::string_view reason) -> bool {
  return reason == "Is a directory";
}

auto output_text_tail(std::istream& in, const TailConfig& config) -> void {
  std::istringstream decoded(read_text_stream(in));
  output_tail(decoded, config);
}

template <size_t N>
auto check_unsupported(const CommandContext<N>&) -> cp::Result<void> {
  return {};
}

template <size_t N>
auto build_config(const CommandContext<N>& ctx) -> cp::Result<TailConfig> {
  TailConfig config;
  config.delimiter = ctx.get<bool>("--zero-terminated", false) ? '\0' : '\n';
  (void)ctx.has("--use-polling");
  config.debug = ctx.get<bool>("--debug", false);

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= N) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    if (option_matches(meta, "-F", "")) {
      config.follow_by_name = true;
      // [GNU] -F is shorthand for --follow=name --retry; --follow=name on
      // its own does not retry.
      config.retry = true;
      continue;
    }

    if (option_matches(meta, "", "--follow")) {
      auto value = std::get_if<std::string>(&occurrence.value);
      if (!value) {
        config.follow_by_name = false;
        continue;
      }
      if (*value == "name") {
        config.follow_by_name = true;
      } else if (*value == "descriptor" || value->empty()) {
        config.follow_by_name = false;
      } else {
        return std::unexpected("invalid follow mode");
      }
    }
  }
  config.follow = ctx.get<bool>("-f", false) || ctx.has("--follow") ||
                  config.follow_by_name;
  config.explicit_retry = ctx.get<bool>("--retry", false);
  config.retry = config.retry || config.explicit_retry;
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
struct FollowTarget {
  std::string file;
  std::optional<FileIdentity> identity;
  std::streampos offset = 0;
  std::ifstream descriptor;
  // [GNU] Count of consecutive polls that saw no size change; when it
  // reaches --max-unchanged-stats the file is reopened by name even though
  // the size still matches, mirroring GNU's periodic fstat refresh.
  std::uintmax_t unchanged_stats = 0;
  bool active = true;
  // [GNU] Whether the name last resolved to a readable file; drives the
  // once-per-transition "has become inaccessible" / "has appeared"
  // diagnostics while --retry keeps polling an absent file.
  bool accessible = true;
};

auto follow_header(const FollowTarget& target, const TailConfig& config)
    -> std::string {
  (void)config;
  // [GNU] Follow headers are always "\n"-terminated; -z applies to
  // output records only (tail.c:413).
  std::string header("\n==> ");
  header += target.file;
  header += " <==\n";
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

auto emit_ignored_pid_warning(const TailConfig& config) -> void {
  if (config.follow || config.follow_pids.empty()) return;
  safeErrorPrint(
      "tail: warning: PID ignored; --pid=PID is useful only when following\n");
}

auto emit_retry_warning(const TailConfig& config) -> void {
  if (!config.explicit_retry) return;
  if (!config.follow) {
    safeErrorPrint(
        "tail: warning: --retry ignored; --retry is useful only when "
        "following\n");
    return;
  }
  if (!config.follow_by_name) {
    safeErrorPrint(
        "tail: warning: --retry only effective for the initial open\n");
  }
}

// [GNU] Shared "name disappeared" diagnostic used while following: with
// --retry the name stays in the follow set and the transition is reported
// once, otherwise the target is dropped like GNU's recheck() does.
auto report_follow_inaccessible(FollowTarget& target, const TailConfig& config)
    -> bool {
  if (config.retry) {
    if (target.accessible) {
      target.accessible = false;
      safeErrorPrint(
          winux::i18n::format("command.tail.follow.inaccessible",
                              "tail: '{}' has become inaccessible: {}\n",
                              target.file, describe_open_failure(target.file)));
    }
    return true;
  }
  safeErrorPrint("tail: ");
  safeErrorPrint(target.file);
  safeErrorPrint(": ");
  safeErrorPrint(describe_open_failure(target.file));
  safeErrorPrint("\n");
  target.active = false;
  return false;
}

auto report_follow_reappeared(FollowTarget& target) -> void {
  target.accessible = true;
  safeErrorPrint(winux::i18n::format(
      "command.tail.follow.appeared",
      "tail: '{}' has appeared;  following new file\n", target.file));
}

auto follow_descriptor_target(FollowTarget& target, const TailConfig& config,
                              bool multi) -> bool {
  if (!target.descriptor.is_open()) {
    target.descriptor = open_input_file(target.file);
    if (!target.descriptor.is_open()) {
      return report_follow_inaccessible(target, config);
    }
    if (!target.accessible) {
      // [GNU] A file that (re)appears while --retry polls is followed from
      // its beginning, not just from its end.
      report_follow_reappeared(target);
      target.offset = 0;
    } else {
      target.descriptor.seekg(0, std::ios::end);
      target.offset = target.descriptor.tellg();
      if (target.offset == std::streampos(-1)) target.offset = 0;
    }
  }

  if (!output_new_data(target.descriptor, target.offset,
                       multi ? follow_header(target, config) : "",
                       target.file)) {
    safeErrorPrint("tail: error reading '");
    safeErrorPrint(target.file);
    safeErrorPrint("' while following\n");
    target.active = false;
    return false;
  }
  return true;
}

// [GNU] --follow=name recheck(): the path is statted by name every poll, so
// replacement (rename+recreate, symlink repoint) is detected as soon as the
// volume/index pair changes rather than only after --max-unchanged-stats
// quiet iterations.
auto follow_name_target(FollowTarget& target, const TailConfig& config,
                        bool multi) -> bool {
  auto current_status = read_file_status(target.file);
  if (!current_status) {
    return report_follow_inaccessible(target, config);
  }

  bool reopen_from_start = false;
  if (!target.accessible) {
    report_follow_reappeared(target);
    target.identity = current_status->identity;
    reopen_from_start = true;
  } else if (target.identity &&
             !same_identity(*target.identity, current_status->identity)) {
    // [GNU] tail.c recheck: diagnose replacement of the followed file name.
    safeErrorPrint(winux::i18n::format(
        "command.tail.replaced",
        "tail: '{}' has been replaced;  following new file\n", target.file));
    target.identity = current_status->identity;
    reopen_from_start = true;
  } else if (!target.identity) {
    target.identity = current_status->identity;
  }

  if (reopen_from_start) {
    target.offset = 0;
    target.unchanged_stats = 0;
  } else if (current_status->size == streampos_to_size(target.offset)) {
    // [GNU] --max-unchanged-stats=N: after N unchanged polls GNU reopens
    // the file because its followed descriptor could be stale.  This
    // implementation stats the path by name every poll, so the identity
    // check above already sees a replacement; the periodic reopen below is
    // kept so a same-size rewrite still produces a fresh open like GNU.
    if (target.unchanged_stats < config.max_unchanged_stats) {
      ++target.unchanged_stats;
      return true;  // Unchanged: nothing new to dump this iteration.
    }
    target.unchanged_stats = 0;
  } else {
    target.unchanged_stats = 0;
  }
  // A shrunken file is reported by output_new_data ("file truncated") and
  // re-read from the start; a grown file dumps only the appended range.

  std::ifstream current = open_input_file(target.file);
  if (!current.is_open()) {
    return report_follow_inaccessible(target, config);
  }

  if (!output_new_data(current, target.offset,
                       multi ? follow_header(target, config) : "",
                       target.file)) {
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
    if (targets.empty() && !ok) {
      // [GNU] Once the last followed name is gone tail gives up with this
      // diagnostic instead of silently exiting the follow loop.
      safeErrorPrint(winux::i18n::format("command.tail.follow.no_files",
                                         "tail: no files remaining\n"));
    }
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
  emit_ignored_pid_warning(config);
  emit_retry_warning(config);

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
    auto emit_header = [&]() {
      if (!show_header) return;
      if (!first_print) safePrint(std::string(1, config.delimiter));
      safePrint("==> ");
      safePrint(file == "-" ? "standard input" : file);
      safePrint(" <==");
      safePrint(std::string(1, config.delimiter));
      first_print = false;
    };

    if (file == "-") {
      emit_header();
      // [GNU] A closed standard input (<&-) is an error, not EOF (#973).
      if (file_io::stdin_is_bad()) {
        safeErrorPrint(
            "tail: cannot fstat 'standard input': Bad file descriptor\n");
        any_error = true;
        continue;
      }
      config.stdin_mode = true;
      if (config.by_bytes || config.delimiter == '\0') {
        output_tail(std::cin, config);
      } else {
        output_text_tail(std::cin, config);
      }
      if (std::cin.bad()) {
        safeErrorPrint("tail: error reading '-'\n");
        any_error = true;
      }
    } else {
      auto input = open_input_file(file);
      if (!input.is_open()) {
        std::string reason = describe_open_failure(file);
        if (is_directory_open_failure(reason)) {
          safeErrorPrint("tail: error reading '");
          safeErrorPrint(file);
          safeErrorPrint("': ");
          safeErrorPrint(reason);
          safeErrorPrint("\n");
        } else {
          safeErrorPrint("tail: cannot open '");
          safeErrorPrint(file);
          safeErrorPrint("' for reading: ");
          safeErrorPrint(reason);
          safeErrorPrint("\n");
        }
        // [GNU] With --retry the failed name stays in the follow set: the
        // initial open error is reported once and every follow iteration
        // retries the open until the file appears.
        if (config.retry && config.follow &&
            !is_directory_open_failure(reason)) {
          FollowTarget target;
          target.file = file;
          target.accessible = false;
          follow_targets_to_run.push_back(std::move(target));
          continue;
        }
        any_error = true;
        continue;
      }

      emit_header();
      bool used_fast_path = false;
      if (config.by_bytes) {
        used_fast_path = output_tail_seekable_bytes(input, config);
      } else if (config.delimiter == '\0') {
        used_fast_path = output_tail_seekable_lines(input, config);
      } else if (!stream_needs_text_decoding(input)) {
        used_fast_path = output_tail_seekable_lines(input, config);
        if (!used_fast_path) output_tail(input, config);
      } else {
        output_text_tail(input, config);
        used_fast_path = true;
      }
      if (!used_fast_path) {
        output_tail(input, config);
      }
      if (input.bad()) {
        safeErrorPrint("tail: error reading '");
        safeErrorPrint(file);
        safeErrorPrint("'\n");
        any_error = true;
      }

      if (config.follow) {
        FollowTarget target;
        target.file = file;
        target.identity = read_file_identity(file);
        // Record the end offset before the stream is moved into the
        // descriptor slot: tellg() on a moved-from stream returns -1 and
        // the follow dump would restart at offset 0, re-printing the file.
        input.clear();
        input.seekg(0, std::ios::end);
        target.offset = input.tellg();
        if (target.offset == std::streampos(-1)) target.offset = 0;
        if (config.follow_by_name) {
          // [GNU] --follow=name always re-opens the path for the first
          // dump; the stream used for the initial output is not reused.
          target.descriptor.close();
        } else {
          target.descriptor = std::move(input);
        }
        follow_targets_to_run.push_back(std::move(target));
      }
    }
  }

  if (config.follow && follow_targets_to_run.empty() && any_error) {
    // [GNU] tail.c: with -f/--follow and no live inputs left, tail ends
    // with "tail: no files remaining" rather than exiting silently.
    safeErrorPrint(winux::i18n::format("command.tail.follow.no_files",
                                       "tail: no files remaining\n"));
  }

  if (!follow_targets_to_run.empty() &&
      !follow_targets(follow_targets_to_run, config)) {
    any_error = true;
  }

  return any_error ? 1 : 0;
}
