// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for split.
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

auto constexpr SPLIT_OPTIONS = std::array{
    // [GNU] -b, --bytes
    OPTION("-b", "--bytes", "put SIZE bytes per output file", STRING_TYPE),
    // [GNU] -C, --line-bytes
    OPTION("-C", "--line-bytes",
           "put at most SIZE bytes of complete lines per output file",
           STRING_TYPE),
    // [GNU] -l, --lines
    OPTION("-l", "--lines", "put NUMBER lines per output file", STRING_TYPE),
    // [GNU] -n, --number
    OPTION("-n", "--number",
           "generate CHUNKS output files; see explanation below", STRING_TYPE),
    // [GNU] -d, --numeric-suffixes
    OPTION("-d", "--numeric-suffixes",
           "use numeric suffixes instead of alphabetic", OPTIONAL_STRING_TYPE),
    // [GNU] -x, --hex-suffixes
    OPTION("-x", "--hex-suffixes", "use hexadecimal suffixes",
           OPTIONAL_STRING_TYPE),
    // [GNU] -a, --suffix-length
    OPTION("-a", "--suffix-length", "use suffixes of length N (default 2)",
           STRING_TYPE),
    // [GNU] --additional-suffix
    OPTION("", "--additional-suffix", "append SUFFIX to output file names",
           STRING_TYPE),
    // [GNU] --filter
    OPTION("", "--filter", "write to shell COMMAND; file name is $FILE",
           STRING_TYPE),
    // [GNU] -e, --elide-empty-files
    OPTION("-e", "--elide-empty-files", "do not generate empty output files"),
    // [GNU] --verbose
    OPTION("", "--verbose",
           "print a diagnostic just before each output file is opened"),
    // [GNU] -t, --separator
    OPTION("-t", "--separator",
           "use SEP instead of newline as the record separator; '\\0' (zero) "
           "specifies the NUL character",
           STRING_TYPE),
    // [GNU] -u, --unbuffered
    OPTION("-u", "--unbuffered",
           "immediately copy input to output with '-n r/...'"),
    // [GNU] -NUMBER: obsolete shorthand for -l NUMBER; hidden like GNU
    OPTION("-NUM", "", "", INT_TYPE),
    // [GNU] ---io-blksize=SIZE: hidden debugging option (triple dash)
    OPTION("", "---io-blksize", "", STRING_TYPE)};

namespace split_pipeline {
namespace cp = core::pipeline;

auto resolve_input_file(const CommandContext<SPLIT_OPTIONS.size()>& ctx)
    -> cp::Result<std::string> {
  if (ctx.positionals.empty()) {
    return "-";
  }

  std::string file_arg = std::string(ctx.positionals[0]);
  if (contains_wildcard(file_arg)) {
    auto glob_result = glob_expand(file_arg);
    if (glob_result.expanded && !glob_result.files.empty()) {
      if (glob_result.files.size() != 1) {
        return std::unexpected("wildcard input must match exactly one file");
      }
      return wstring_to_utf8(glob_result.files[0]);
    }
  }

  return file_arg;
}

struct Config {
  enum class Mode { Lines, Bytes, LineBytes, Number };
  enum class SuffixKind { Alpha, Numeric, Hex };
  enum class NumberMode { ApproximateBytes, PreserveRecords, RoundRobin };

  Mode mode = Mode::Lines;
  int64_t chunk_size = 0;
  int64_t chunk_lines = 1000;             // Default: 1000 lines per file
  int64_t num_chunks = 0;                 // For -n mode
  std::optional<int64_t> selected_chunk;  // GNU k/N forms, 1-based
  NumberMode number_mode = NumberMode::ApproximateBytes;
  SuffixKind suffix_kind = SuffixKind::Alpha;
  int suffix_length = 2;
  bool suffix_length_explicit = false;
  uint64_t suffix_start = 0;
  bool suffix_start_explicit = false;
  std::string additional_suffix;
  std::string prefix = "x";
  std::string input_file;
  std::string filter_command;
  bool elide_empty = false;
  bool verbose = false;
  char separator = '\n';  // Default: newline
  bool separator_explicit = false;
  bool unbuffered = false;
};

auto checked_mul(int64_t value, int64_t multiplier) -> cp::Result<int64_t> {
  if (value <= 0 || multiplier <= 0 ||
      value > std::numeric_limits<int64_t>::max() / multiplier) {
    return std::unexpected("invalid size");
  }
  return value * multiplier;
}

auto strip_leading_plus(std::string_view value) -> std::string_view {
  if (!value.empty() && value.front() == '+') {
    value.remove_prefix(1);
  }
  return value;
}

// Mirrors GNU split's strtoint_die: every rejected NUMBER is diagnosed as
// "<msgid>: '<arg>'" with the offending argument quoted as a whole.
auto quoted_number_error(std::string_view msgid, std::string_view arg)
    -> std::string {
  return std::string(msgid) + ": '" + std::string(arg) + "'";
}

auto parse_i64_full(std::string_view value) -> cp::Result<int64_t> {
  value = strip_leading_plus(value);
  int64_t parsed = 0;
  auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec != std::errc() || ptr != value.data() + value.size()) {
    return std::unexpected("invalid integer");
  }
  return parsed;
}

auto parse_size(const std::string& size_str) -> cp::Result<int64_t> {
  std::string s = size_str;
  if (s.empty()) return std::unexpected("invalid size");

  int64_t multiplier = 1;
  auto consume_suffix = [&](std::string_view suffix, int64_t factor) {
    if (s.size() < suffix.size()) return false;
    if (std::string_view(s).substr(s.size() - suffix.size()) != suffix) {
      return false;
    }
    multiplier = factor;
    s.resize(s.size() - suffix.size());
    return true;
  };

  struct Suffix {
    std::string_view text;
    int64_t factor;
  };
  constexpr int64_t k1000 = 1000;
  constexpr int64_t k1024 = 1024;
  const std::array<Suffix, 31> suffixes = {
      Suffix{"KiB", k1024},
      Suffix{"kiB", k1024},
      Suffix{"MiB", k1024 * k1024},
      Suffix{"miB", k1024 * k1024},
      Suffix{"GiB", k1024 * k1024 * k1024},
      Suffix{"giB", k1024 * k1024 * k1024},
      Suffix{"TiB", k1024 * k1024 * k1024 * k1024},
      Suffix{"tiB", k1024 * k1024 * k1024 * k1024},
      Suffix{"PiB", k1024 * k1024 * k1024 * k1024 * k1024},
      Suffix{"piB", k1024 * k1024 * k1024 * k1024 * k1024},
      Suffix{"EiB", k1024 * k1024 * k1024 * k1024 * k1024 * k1024},
      Suffix{"eiB", k1024 * k1024 * k1024 * k1024 * k1024 * k1024},
      Suffix{"KB", k1000},
      Suffix{"MB", k1000 * k1000},
      Suffix{"GB", k1000 * k1000 * k1000},
      Suffix{"TB", k1000 * k1000 * k1000 * k1000},
      Suffix{"PB", k1000 * k1000 * k1000 * k1000 * k1000},
      Suffix{"EB", k1000 * k1000 * k1000 * k1000 * k1000 * k1000},
      Suffix{"K", k1024},
      Suffix{"M", k1024 * k1024},
      Suffix{"G", k1024 * k1024 * k1024},
      Suffix{"T", k1024 * k1024 * k1024 * k1024},
      Suffix{"P", k1024 * k1024 * k1024 * k1024 * k1024},
      Suffix{"E", k1024 * k1024 * k1024 * k1024 * k1024 * k1024},
      Suffix{"k", k1024},
      Suffix{"m", k1024 * k1024},
      // [GNU] the multiplier alphabet is exactly "bEGKkMmPQRTYZ0"
      // (split.c:1355): lowercase e/g/t/p are NOT suffixes, and the
      // Q/R/Y/Z powers (1024^7 and up) exist -- they overflow int64
      // for any nonzero value, so they carry a saturate sentinel
      // (GNU parses them with OVERFLOW_OK) (audit #1462).
      Suffix{"Q", -1},
      Suffix{"R", -1},
      Suffix{"Y", -1},
      Suffix{"Z", -1},
      Suffix{"b", 512},
  };

  for (const auto& suffix : suffixes) {
    if (consume_suffix(suffix.text, suffix.factor)) break;
  }

  if (s.empty()) return std::unexpected("invalid size");
  auto value_text = strip_leading_plus(s);
  int64_t value = 0;
  auto [ptr, ec] = std::from_chars(
      value_text.data(), value_text.data() + value_text.size(), value);
  // [GNU] parse_n_units requires a FULL xstrtoimax parse: trailing
  // junk ("10junk", "1.5K") is invalid, not a silent truncation
  // (audit #1462).
  if (ec != std::errc() || ptr != value_text.data() + value_text.size()) {
    return std::unexpected("invalid size format");
  }
  if (multiplier == -1) {
    // Q/R/Y/Z sentinel: any positive value saturates, as with GNU's
    // OVERFLOW_OK parse.
    return value > 0 ? std::numeric_limits<int64_t>::max() : value;
  }
  return checked_mul(value, multiplier);
}

auto parse_suffix_start(const std::string& value, int base)
    -> cp::Result<uint64_t> {
  if (value.empty()) return 0;
  auto text = strip_leading_plus(value);
  if (base == 16 && text.size() > 2 && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2);
  }
  uint64_t parsed = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), parsed, base);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return std::unexpected("invalid suffix start");
  }
  return parsed;
}

auto build_config(const CommandContext<SPLIT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto bytes_opt = ctx.get<std::string>("--bytes", "");
  if (bytes_opt.empty()) bytes_opt = ctx.get<std::string>("-b", "");
  auto line_bytes_opt = ctx.get<std::string>("--line-bytes", "");
  if (line_bytes_opt.empty()) {
    line_bytes_opt = ctx.get<std::string>("-C", "");
  }
  auto lines_opt = ctx.get<std::string>("--lines", "");
  if (lines_opt.empty()) lines_opt = ctx.get<std::string>("-l", "");

  auto number_opt = ctx.get<std::string>("--number", "");
  if (number_opt.empty()) number_opt = ctx.get<std::string>("-n", "");

  // [GNU] obsolete "-NUMBER" operand is another way to request -l NUMBER;
  // it conflicts with any other split mode like -l itself does.
  const bool has_numeric_lines = ctx.has("-NUM");
  const int numeric_lines = ctx.get<int>("-NUM", 0);

  // [GNU] FAIL_ONLY_ONE_WAY (split.c:1290-1295) counts every mode
  // SELECTION, including a repeated option of the same kind: -l 2 -l 3
  // dies too. Presence -- not a non-empty last value -- is what counts,
  // so an explicitly empty mode value (-b '') is a selection that then
  // fails its own parse (audit #1462).
  // Count OCCURRENCES via ctx.count: the framework resolves the
  // short and long spelling of an option to the SAME meta slot, so
  // summing get_all() over both spellings counts every selection
  // twice and made every single-mode invocation "more than one way".
  auto occurrences_of = [&](const char* lng, const char* sht) {
    return ctx.count({lng, sht});
  };
  const size_t bytes_count = occurrences_of("--bytes", "-b");
  const size_t line_bytes_count = occurrences_of("--line-bytes", "-C");
  const size_t lines_count = occurrences_of("--lines", "-l");
  const size_t number_count = occurrences_of("--number", "-n");
  const size_t numeric_count = ctx.get_all<int>("-NUM").size();
  const int split_modes = static_cast<int>(bytes_count + line_bytes_count +
                                           lines_count + number_count +
                                           numeric_count);
  if (split_modes > 1) {
    return std::unexpected(
        "cannot split in more than one way\nTry 'split --help' for more "
        "information.");
  }
  const bool bytes_given = bytes_count > 0;
  const bool line_bytes_given = line_bytes_count > 0;
  const bool lines_given = lines_count > 0;
  const bool number_given = number_count > 0;

  if (bytes_given) {
    auto size_result = parse_size(bytes_opt);
    if (!size_result || *size_result <= 0) {
      // GNU parse_n_units(msgid="invalid number of bytes"): parse failures,
      // bad suffixes, overflow, and non-positive values all quote the arg.
      return std::unexpected(
          quoted_number_error("invalid number of bytes", bytes_opt));
    }
    cfg.chunk_size = *size_result;
    cfg.mode = Config::Mode::Bytes;
  }

  if (line_bytes_given) {
    auto size_result = parse_size(line_bytes_opt);
    if (!size_result || *size_result <= 0) {
      // GNU -C reuses the "invalid number of lines" message for line bytes.
      return std::unexpected(
          quoted_number_error("invalid number of lines", line_bytes_opt));
    }
    cfg.chunk_size = *size_result;
    cfg.mode = Config::Mode::LineBytes;
  }

  if (lines_given) {
    auto lines_result = parse_i64_full(lines_opt);
    if (!lines_result || *lines_result <= 0) {
      return std::unexpected(
          quoted_number_error("invalid number of lines", lines_opt));
    }
    cfg.chunk_lines = *lines_result;
    cfg.mode = Config::Mode::Lines;
  }

  if (has_numeric_lines) {
    if (numeric_lines <= 0) {
      return std::unexpected(quoted_number_error(
          "invalid number of lines", std::to_string(numeric_lines)));
    }
    cfg.chunk_lines = numeric_lines;
    cfg.mode = Config::Mode::Lines;
  }

  if (number_given) {
    // GNU -n supports: N, k/N, l/N, l/k/N, r/N, and r/k/N. Mirrors GNU
    // parse_chunk: a pure number is k = n; in k/N form the k part must be
    // pure digits (otherwise the whole argument is rejected as an invalid
    // number of chunks), the N part must parse fully, and k must satisfy
    // 0 < k <= n ("invalid chunk number" quotes only the k part).
    bool line_mode = false;
    bool round_robin = false;
    std::string val = number_opt;

    // [GNU] only the exact "l/" and "r/" prefixes select those modes;
    // "l2" falls into parse_chunk and dies as an invalid number of
    // chunks (audit #1462).
    if (val.size() >= 2 && val[1] == '/' && val[0] == 'l') {
      line_mode = true;
      val = val.substr(2);
    } else if (val.size() >= 2 && val[1] == '/' && val[0] == 'r') {
      round_robin = true;
      val = val.substr(2);
    }

    auto fail_chunks = [&](std::string_view arg) {
      return quoted_number_error("invalid number of chunks", arg);
    };

    const size_t slash = val.find('/');
    if (slash == std::string::npos) {
      auto num_result = parse_i64_full(val);
      if (!num_result || *num_result <= 0) {
        return std::unexpected(fail_chunks(val));
      }
      cfg.num_chunks = *num_result;
    } else {
      const std::string k_text(val.substr(0, slash));
      const std::string n_text(val.substr(slash + 1));
      auto k_result = parse_i64_full(k_text);
      if (!k_result) {
        return std::unexpected(fail_chunks(val));
      }
      auto n_result = parse_i64_full(n_text);
      if (!n_result || *n_result <= 0) {
        return std::unexpected(fail_chunks(n_text));
      }
      if (*k_result <= 0 || *k_result > *n_result) {
        return std::unexpected(
            quoted_number_error("invalid chunk number", k_text));
      }
      cfg.selected_chunk = *k_result;
      cfg.num_chunks = *n_result;
    }
    cfg.mode = Config::Mode::Number;
    cfg.number_mode = round_robin ? Config::NumberMode::RoundRobin
                      : line_mode ? Config::NumberMode::PreserveRecords
                                  : Config::NumberMode::ApproximateBytes;
  }

  // [GNU] getopt processes -d and -x in argv order; the LAST of them
  // sets the suffix alphabet (split -x -d produces numeric suffixes).
  // The old code checked numeric first and hex second unconditionally
  // (audit #1462).
  {
    bool suffix_kind_seen = false;
    std::string numeric_start;
    std::string hex_start;
    if (ctx.metas) {
      for (const auto& occurrence : ctx.options.occurrences()) {
        if (occurrence.index >= ctx.metas->size()) continue;
        const auto& meta = (*ctx.metas)[occurrence.index];
        if (meta.short_name == "-d" || meta.long_name == "--numeric-suffixes") {
          cfg.suffix_kind = Config::SuffixKind::Numeric;
          suffix_kind_seen = true;
          numeric_start =
              std::get_if<std::string>(&occurrence.value)
                  ? *std::get_if<std::string>(&occurrence.value)
                  : "";
        } else if (meta.short_name == "-x" ||
                   meta.long_name == "--hex-suffixes") {
          cfg.suffix_kind = Config::SuffixKind::Hex;
          suffix_kind_seen = true;
          hex_start = std::get_if<std::string>(&occurrence.value)
                          ? *std::get_if<std::string>(&occurrence.value)
                          : "";
        }
      }
    }
    if (!suffix_kind_seen &&
        (ctx.has("--numeric-suffixes") || ctx.has("-d") ||
         ctx.has("--hex-suffixes") || ctx.has("-x"))) {
      // Fallback when no occurrence metadata is available.
      if (ctx.has("--hex-suffixes") || ctx.has("-x")) {
        cfg.suffix_kind = Config::SuffixKind::Hex;
        hex_start = ctx.get<std::string>("--hex-suffixes", "");
        if (hex_start.empty()) hex_start = ctx.get<std::string>("-x", "");
      } else {
        cfg.suffix_kind = Config::SuffixKind::Numeric;
        numeric_start = ctx.get<std::string>("--numeric-suffixes", "");
        if (numeric_start.empty()) {
          numeric_start = ctx.get<std::string>("-d", "");
        }
      }
      suffix_kind_seen = true;
    }
    if (suffix_kind_seen) {
      const bool hex = cfg.suffix_kind == Config::SuffixKind::Hex;
      const std::string& start = hex ? hex_start : numeric_start;
      auto start_result = parse_suffix_start(start, hex ? 16 : 10);
      if (!start_result) return std::unexpected(start_result.error());
      cfg.suffix_start = *start_result;
      cfg.suffix_start_explicit = !start.empty();
    }
  }

  auto suffix_opt = ctx.get<std::string>("--suffix-length", "");
  if (suffix_opt.empty()) {
    suffix_opt = ctx.get<std::string>("-a", "");
  }
  if (ctx.has("--suffix-length") || ctx.has("-a")) {
    auto suffix_length = parse_i64_full(suffix_opt);
    // [GNU] xdectoimax (optarg, 0, IDX_MAX, ..., "invalid suffix
    // length"): no 32 cap (audit #1462).
    if (!suffix_length || *suffix_length < 0 ||
        *suffix_length > std::numeric_limits<int>::max()) {
      return std::unexpected("invalid suffix length");
    }
    cfg.suffix_length = static_cast<int>(*suffix_length);
    cfg.suffix_length_explicit = cfg.suffix_length != 0;
    if (cfg.suffix_length == 0) cfg.suffix_length = 2;
  }

  cfg.additional_suffix = ctx.get<std::string>("--additional-suffix", "");
  if (cfg.additional_suffix.find('/') != std::string::npos ||
      cfg.additional_suffix.find('\\') != std::string::npos) {
    return std::unexpected("additional suffix must not contain slash");
  }

  // [GNU] ---io-blksize=SIZE is a hidden debugging option: the size is
  // validated like -b but only tunes the I/O block size, so the parsed
  // value is intentionally unused here.
  if (ctx.has("---io-blksize")) {
    auto io_blksize = ctx.get<std::string>("---io-blksize", "");
    auto size_result = parse_size(io_blksize);
    if (!size_result || *size_result <= 0) {
      return std::unexpected(
          quoted_number_error("invalid IO block size", io_blksize));
    }
  }

  cfg.filter_command = ctx.get<std::string>("--filter", "");
  cfg.elide_empty = ctx.has("--elide-empty-files") || ctx.has("-e");
  cfg.verbose = ctx.has("--verbose");
  cfg.unbuffered = ctx.has("--unbuffered") || ctx.has("-u");

  const bool has_long_separator = ctx.has("--separator");
  const bool has_short_separator = ctx.has("-t");
  if (has_long_separator || has_short_separator) {
    cfg.separator_explicit = true;
    // [GNU] split.c:1457-1479: the separator is exactly one character
    // or the two-character spelling "\0" for NUL. The "\n"/"\t"
    // spellings the old code mapped are multi-character separators
    // in GNU, an empty value is its own diagnostic, and two
    // DIFFERENT -t values die as well (audit #1462).
    // One get_all by either spelling already returns every
    // occurrence of the shared meta slot; concatenating both
    // spellings duplicated each value.
    auto sep_values = ctx.get_all<std::string>("--separator");
    std::optional<std::string> sep_opt;
    for (const auto& value : sep_values) {
      if (sep_opt && value != *sep_opt) {
        return std::unexpected("multiple separator characters specified");
      }
      sep_opt = value;
    }
    if (!sep_opt || sep_opt->empty()) {
      return std::unexpected("empty record separator");
    }
    if (*sep_opt == "\\0") {
      cfg.separator = '\0';
    } else if (sep_opt->size() == 1) {
      cfg.separator = (*sep_opt)[0];
    } else {
      return std::unexpected("multi-character separator '" + *sep_opt + "'");
    }
  }

  auto input_result = resolve_input_file(ctx);
  if (!input_result) {
    return std::unexpected(input_result.error());
  }
  cfg.input_file = *input_result;

  if (ctx.positionals.size() > 1) {
    cfg.prefix = std::string(ctx.positionals[1]);
  }

  return cfg;
}

auto convert_unsigned(uint64_t value, uint32_t base) -> std::string {
  constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string result;
  do {
    result.push_back(digits[value % base]);
    value /= base;
  } while (value != 0);
  std::reverse(result.begin(), result.end());
  return result;
}

auto alpha_suffix(uint64_t value, int length) -> cp::Result<std::string> {
  uint64_t capacity = 1;
  for (int i = 0; i < length; ++i) {
    if (capacity > std::numeric_limits<uint64_t>::max() / 26) {
      return std::unexpected("output file suffixes exhausted");
    }
    capacity *= 26;
  }
  if (value >= capacity) {
    return std::unexpected("output file suffixes exhausted");
  }

  std::string result(static_cast<size_t>(length), 'a');
  for (int i = length - 1; i >= 0; --i) {
    result[static_cast<size_t>(i)] = static_cast<char>('a' + value % 26);
    value /= 26;
  }
  return result;
}

auto generate_suffix(const Config& cfg, uint64_t part_num)
    -> cp::Result<std::string> {
  uint64_t value = cfg.suffix_start + part_num;
  if (value < cfg.suffix_start) {
    return std::unexpected("output file suffixes exhausted");
  }

  if (cfg.suffix_kind == Config::SuffixKind::Alpha) {
    int length = cfg.suffix_length;
    auto result = alpha_suffix(value, length);
    while (!result && !cfg.suffix_length_explicit && length < 32) {
      // [GNU] next_file_name widens by ONE: after xzz comes xaaa,
      // not a 4-character suffix (audit #1462).
      length += 1;
      result = alpha_suffix(value, length);
    }
    return result;
  }

  uint32_t base = cfg.suffix_kind == Config::SuffixKind::Hex ? 16 : 10;
  std::string suffix = convert_unsigned(value, base);
  int length = cfg.suffix_length;
  if (suffix.size() > static_cast<size_t>(length)) {
    if (cfg.suffix_length_explicit || cfg.suffix_start_explicit) {
      return std::unexpected("output file suffixes exhausted");
    }
    length = static_cast<int>(suffix.size());
  }
  if (suffix.size() < static_cast<size_t>(length)) {
    suffix.insert(suffix.begin(), static_cast<size_t>(length) - suffix.size(),
                  '0');
  }
  return suffix;
}

auto make_filename(const Config& cfg, uint64_t part_num)
    -> cp::Result<std::string> {
  auto suffix = generate_suffix(cfg, part_num);
  if (!suffix) return std::unexpected(suffix.error());
  return cfg.prefix + *suffix + cfg.additional_suffix;
}

auto write_chunk(const Config& cfg, uint64_t part_num, std::string_view chunk)
    -> cp::Result<bool> {
  auto filename_result = make_filename(cfg, part_num);
  if (!filename_result) return std::unexpected(filename_result.error());
  const auto& filename = *filename_result;

  if (cfg.elide_empty && chunk.empty()) {
    return false;  // Skip empty files
  }

  if (cfg.verbose) {
    // [GNU] split.c create() prints "creating file '<name>'" to
    // stdout, not stderr (audit #1462).
    safePrint("creating file '" + filename + "'\n");
  }

  if (!cfg.filter_command.empty()) {
    // GNU split exports FILE to the filter command environment. Keep the
    // existing $FILE textual substitution too so common Windows cmd filters
    // remain usable under the current _popen-based shell path.
    // The filter string is run through the command interpreter (cmd.exe /c),
    // whose own search cannot parse POSIX '/c/...' PATH entries and would
    // otherwise resolve the first word behind System32 images. Rewriting the
    // first word to its PATH-resolved absolute form restores GNU execvp
    // resolution; unresolved words are left for the shell to report.
    std::string cmd =
        winux::childproc::resolve_shell_command_first_word(cfg.filter_command);
    size_t pos;
    while ((pos = cmd.find("$FILE")) != std::string::npos) {
      cmd.replace(pos, 5, filename);
    }

    const char* previous_file = std::getenv("FILE");
    std::optional<std::string> previous_file_value;
    if (previous_file != nullptr) {
      previous_file_value = previous_file;
    }
    _putenv_s("FILE", filename.c_str());

    FILE* pipe = _popen(cmd.c_str(), "w");
    if (!pipe) {
      if (previous_file_value.has_value()) {
        _putenv_s("FILE", previous_file_value->c_str());
      } else {
        _putenv_s("FILE", "");
      }
      return std::unexpected(std::string("cannot run filter command"));
    }
    // [#1214] _popen() hands back a CRT stream in text mode, so every 0x0A in
    // the chunk reached the filter as CRLF — `split --filter=cat` emitted
    // a\r\nb\r\n where GNU emits a\nb\n.  The filter must receive the exact
    // bytes GNU popen() would deliver.
    _setmode(_fileno(pipe), _O_BINARY);
    fwrite(chunk.data(), 1, chunk.size(), pipe);
    int ret = _pclose(pipe);
    if (previous_file_value.has_value()) {
      _putenv_s("FILE", previous_file_value->c_str());
    } else {
      _putenv_s("FILE", "");
    }
    if (ret != 0) {
      return std::unexpected(std::string("filter command exited with status ") +
                             std::to_string(ret));
    }
    return true;
  }

  std::ofstream out(filename, std::ios::binary);
  if (!out) {
    return std::unexpected(std::string("cannot create '") + filename + "'");
  }
  out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
  if (!out) {
    return std::unexpected(std::string("error writing '") + filename + "'");
  }
  return true;
}

struct RecordSpan {
  size_t start;
  size_t end;
};

auto collect_record_spans(std::string_view input, char separator)
    -> std::vector<RecordSpan> {
  std::vector<RecordSpan> records;
  for (size_t pos = 0; pos < input.size();) {
    size_t next = input.find(separator, pos);
    size_t end = next == std::string::npos ? input.size() : next + 1;
    records.push_back({pos, end});
    pos = end;
  }
  return records;
}

auto run(const Config& cfg) -> int {
  file_io::set_stdin_binary();
  // Stream the common fixed-size modes. The filter and record-preserving
  // number modes still use the existing in-memory path because their output
  // contract requires a complete record set or a subprocess payload.
  if (cfg.filter_command.empty() &&
      (cfg.mode == Config::Mode::Lines || cfg.mode == Config::Mode::Bytes ||
       cfg.mode == Config::Mode::LineBytes)) {
    std::ifstream input;
    if (cfg.input_file.empty() || cfg.input_file == "-") {
      // stdin is already a stream and is handled by the same byte loop below.
    } else {
      auto operand = native_path::make_api_path_operand(cfg.input_file);
      input.open(std::filesystem::path(operand.extended), std::ios::binary);
      if (!input) {
        std::string reason = "No such file or directory";
        const DWORD attrs = native_path::attributes_w(operand.extended);
        if (native_path::attributes_are_directory(attrs)) {
          reason = "Is a directory";
        }
        cp::Result<int> result = std::unexpected(
            "cannot open '" + cfg.input_file + "' for reading: " + reason);
        cp::report_error(result, L"split");
        return 1;
      }
    }
    std::istream& source = input.is_open()
                               ? static_cast<std::istream&>(input)
                               : static_cast<std::istream&>(std::cin);
    uint64_t part_num = 0;
    std::string chunk;
    int64_t records = 0;
    size_t used = 0;
    std::array<char, 64 * 1024> buffer{};
    auto flush = [&]() -> bool {
      if (chunk.empty()) return true;
      auto result = write_chunk(cfg, part_num, chunk);
      if (!result) {
        cp::report_error(result, L"split");
        return false;
      }
      if (*result) ++part_num;
      chunk.clear();
      used = 0;
      records = 0;
      return true;
    };
    if (cfg.mode == Config::Mode::Bytes) {
      while (source) {
        source.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = source.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
          chunk.push_back(buffer[static_cast<size_t>(i)]);
          if (chunk.size() == static_cast<size_t>(cfg.chunk_size) && !flush())
            return 1;
        }
      }
    } else {
      std::string record;
      char c = 0;
      auto add_record = [&](const std::string& rec) -> bool {
        const auto limit = static_cast<size_t>(cfg.chunk_size);
        if (!chunk.empty() && chunk.size() + rec.size() > limit && !flush()) {
          return false;
        }
        if (rec.size() <= limit) {
          chunk += rec;
          return true;
        }
        // [GNU] -C never emits a piece larger than LIMIT: a single line
        // longer than LIMIT is split at exact LIMIT boundaries
        // (uutils #7262).
        size_t pos = 0;
        if (!chunk.empty()) {
          const size_t take = limit - chunk.size();
          chunk.append(rec, 0, take);
          pos = take;
          if (!flush()) return false;
        }
        while (rec.size() - pos >= limit) {
          chunk.append(rec, pos, limit);
          pos += limit;
          if (!flush()) return false;
        }
        chunk.append(rec, pos, rec.size() - pos);
        return true;
      };
      while (source.get(c)) {
        record.push_back(c);
        if (c != cfg.separator) continue;

        if (cfg.mode == Config::Mode::Lines) {
          chunk += record;
          record.clear();
          ++records;
          if (records >= cfg.chunk_lines && !flush()) return 1;
        } else {
          if (!add_record(record)) return 1;
          record.clear();
        }
      }
      if (!record.empty()) {
        if (cfg.mode == Config::Mode::LineBytes) {
          if (!add_record(record)) return 1;
        } else {
          chunk += record;
        }
      }
      if (!chunk.empty() && !flush()) return 1;
    }
    if (!chunk.empty() && !flush()) return 1;
    if (source.bad()) {
      cp::Result<int> result = std::unexpected("error reading from file");
      cp::report_error(result, L"split");
      return 1;
    }
    return 0;
  }

  // Read input
  std::string input;
  auto split_input_open_error = [](std::string_view path) -> std::string {
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::u8path(path), ec) &&
        !ec) {
      return std::string("cannot open '") + std::string(path) +
             "' for reading: Is a directory";
    }

    auto operand = native_path::make_api_path_operand(path);
    const DWORD attrs = native_path::attributes_w(operand.extended);
    const std::string reason = native_path::attributes_are_directory(attrs)
                                   ? "Is a directory"
                                   : "No such file or directory";
    return std::string("cannot open '") + std::string(path) +
           "' for reading: " + reason;
  };

  if (cfg.input_file.empty() || cfg.input_file == "-") {
    // Read from stdin
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      cp::Result<int> result = std::unexpected("error reading from file");
      cp::report_error(result, L"split");
      return 1;
    }
  } else {
    // Read from file. #1288: open the operand's extended spelling like
    // the streaming path above; the raw narrow operand fails past
    // MAX_PATH and on '/'-spelled paths.
    std::ifstream f(
        std::filesystem::path(
            native_path::make_api_path_operand(cfg.input_file).extended),
        std::ios::binary);
    if (!f) {
      auto err = split_input_open_error(cfg.input_file);
      cp::Result<int> result = std::unexpected(err);
      cp::report_error(result, L"split");
      return 1;
    }
    input.assign(std::istreambuf_iterator<char>(f),
                 std::istreambuf_iterator<char>());
    if (f.fail() && !f.eof()) {
      cp::Result<int> result = std::unexpected("error reading from file");
      cp::report_error(result, L"split");
      return 1;
    }
  }

  if (input.empty() && cfg.mode != Config::Mode::Number) {
    return 0;  // Nothing to split
  }
  // [GNU] every -n form creates all N output files even for empty
  // input (bytes_split/lines_chunk_split "ensure files are created"
  // loops, lines_rr pre-creation), subject to -e (audit #1462); the
  // Number branches below write every chunk unconditionally.

  uint64_t part_num = 0;
  if (cfg.mode == Config::Mode::Lines) {
    size_t start = 0;
    int64_t lines_in_chunk = 0;
    char sep = cfg.separator;
    for (size_t pos = 0; pos < input.size();) {
      size_t next = input.find(sep, pos);
      size_t record_end = next == std::string::npos ? input.size() : next + 1;
      ++lines_in_chunk;
      pos = record_end;

      if (lines_in_chunk == cfg.chunk_lines || pos == input.size()) {
        auto result = write_chunk(
            cfg, part_num,
            std::string_view(input.data() + start, record_end - start));
        if (!result) {
          cp::report_error(result, L"split");
          return 1;
        }
        if (*result) ++part_num;
        start = record_end;
        lines_in_chunk = 0;
      }
    }
  } else if (cfg.mode == Config::Mode::Number) {
    auto write_number_chunk = [&](std::string_view chunk) -> bool {
      auto result = write_chunk(cfg, part_num, chunk);
      if (!result) {
        cp::report_error(result, L"split");
        return false;
      }
      if (*result) ++part_num;
      return true;
    };

    if (cfg.number_mode == Config::NumberMode::PreserveRecords) {
      auto records = collect_record_spans(input, cfg.separator);

      // [GNU] lines_chunk_split partitions the input BYTES into N
      // near-equal ranges (the first size%N ranges are one byte
      // longer); a record belongs to the file in whose range the
      // record STARTS, so files hold varying record counts. The old
      // code partitioned by record index, giving every file an
      // equal number of records (audit #1462; verified against the
      // 9.4 binary).
      const size_t total_bytes = input.size();
      const size_t n = static_cast<size_t>(cfg.num_chunks);
      const size_t base = n == 0 ? 0 : total_bytes / n;
      const size_t rem = n == 0 ? 0 : total_bytes % n;
      auto range_start = [&](size_t i) {
        return i * base + std::min(i, rem);
      };

      auto record_chunk = [&](int64_t index) -> std::string_view {
        const size_t lo = range_start(static_cast<size_t>(index));
        const size_t hi = range_start(static_cast<size_t>(index) + 1);
        size_t start = std::string::npos;
        size_t end = 0;
        for (const auto& record : records) {
          if (record.start >= lo && record.start < hi) {
            if (start == std::string::npos) start = record.start;
            end = record.end;
          }
        }
        if (start == std::string::npos) return {};
        return std::string_view(input.data() + start, end - start);
      };

      if (cfg.selected_chunk.has_value()) {
        safePrint(record_chunk(*cfg.selected_chunk - 1));
      } else {
        for (int64_t i = 0; i < cfg.num_chunks; ++i) {
          if (!write_number_chunk(record_chunk(i))) return 1;
        }
      }
    } else if (cfg.number_mode == Config::NumberMode::RoundRobin) {
      auto records = collect_record_spans(input, cfg.separator);
      const size_t num_chunks = static_cast<size_t>(cfg.num_chunks);

      if (cfg.selected_chunk.has_value()) {
        const size_t selected = static_cast<size_t>(*cfg.selected_chunk - 1);
        for (size_t i = selected; i < records.size(); i += num_chunks) {
          const auto& record = records[i];
          safePrint(std::string_view(input.data() + record.start,
                                     record.end - record.start));
        }
      } else {
        std::vector<std::string> chunks(num_chunks);
        for (size_t i = 0; i < records.size(); ++i) {
          const auto& record = records[i];
          chunks[i % num_chunks].append(input.data() + record.start,
                                        record.end - record.start);
        }
        for (const auto& chunk : chunks) {
          if (!write_number_chunk(chunk)) return 1;
        }
      }
    } else {
      // [GNU] bytes_split is called with (file_size / N, file_size %
      // N): the first rem chunks get one extra byte (10 bytes over
      // -n 3 split 4/3/3); the old code gave the whole remainder to
      // the last chunk (3/3/4) (audit #1462; verified against the
      // 9.4 binary). All N chunks are written, including empty tail
      // chunks when the input has fewer bytes than N.
      const size_t total_bytes = input.size();
      const size_t n = static_cast<size_t>(cfg.num_chunks);
      const size_t base = n == 0 ? 0 : total_bytes / n;
      const size_t rem = n == 0 ? 0 : total_bytes % n;
      auto range_start = [&](size_t i) {
        return i * base + std::min(i, rem);
      };

      auto byte_chunk = [&](int64_t index) -> std::string_view {
        const size_t start = range_start(static_cast<size_t>(index));
        const size_t end = range_start(static_cast<size_t>(index) + 1);
        if (start >= total_bytes) return {};
        return std::string_view(input.data() + start,
                                std::min(end, total_bytes) - start);
      };

      if (cfg.selected_chunk.has_value()) {
        safePrint(byte_chunk(*cfg.selected_chunk - 1));
      } else {
        for (int64_t i = 0; i < cfg.num_chunks; ++i) {
          if (!write_number_chunk(byte_chunk(i))) return 1;
        }
      }
    }
  } else if (cfg.mode == Config::Mode::Bytes) {
    for (size_t pos = 0; pos < input.size();) {
      size_t chunk_size = static_cast<size_t>(std::min<int64_t>(
          cfg.chunk_size, static_cast<int64_t>(input.size() - pos)));
      auto result = write_chunk(
          cfg, part_num, std::string_view(input.data() + pos, chunk_size));
      if (!result) {
        cp::report_error(result, L"split");
        return 1;
      }
      if (*result) ++part_num;
      pos += chunk_size;
    }
  } else {
    char sep = cfg.separator;
    for (size_t pos = 0; pos < input.size();) {
      size_t start = pos;
      size_t used = 0;

      while (pos < input.size()) {
        size_t next = input.find(sep, pos);
        size_t record_end = next == std::string::npos ? input.size() : next + 1;
        size_t record_size = record_end - pos;

        if (record_size > static_cast<size_t>(cfg.chunk_size)) {
          if (used == 0) pos += static_cast<size_t>(cfg.chunk_size);
          break;
        }
        if (used != 0 &&
            used + record_size > static_cast<size_t>(cfg.chunk_size)) {
          break;
        }

        used += record_size;
        pos = record_end;
        if (used == static_cast<size_t>(cfg.chunk_size)) break;
      }

      auto result = write_chunk(
          cfg, part_num, std::string_view(input.data() + start, pos - start));
      if (!result) {
        cp::report_error(result, L"split");
        return 1;
      }
      if (*result) ++part_num;
    }
  }

  return 0;
}

}  // namespace split_pipeline

REGISTER_COMMAND(
    split, "split", "split [OPTION]... [INPUT [PREFIX]]",
    "Output fixed-size pieces of INPUT to PREFIXaa, PREFIXab, ...\n"
    "\n"
    "By default, split puts 1000 lines of INPUT (or stdin) into each output "
    "file.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "SIZE may have a multiplier suffix: b for 512, K for 1K, M for 1M, G for "
    "1G, etc.\n"
    "\n"
    "Note: This implementation supports common GNU line, byte, line-byte, "
    "number, filter, separator, and suffix options.",
    "  split -l 1000 largefile.txt\n"
    "  split -b 100M largefile.txt\n"
    "  split -C 64K log.txt chunk-\n"
    "  split -n 5 largefile.txt\n"
    "  split -d -a 3 largefile.txt part\n"
    "  split --filter='gzip > $FILE.gz' -b 1M input.dat\n"
    "  split -t '\\0' null-delimited.txt\n"
    "  split -b 1M -x --additional-suffix=.bin input.dat output",
    "csplit(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SPLIT_OPTIONS) {
  using namespace split_pipeline;

  if (ctx.positionals.size() > 2) {
    safeErrorPrint("split: extra operand '");
    safeErrorPrint(std::string(ctx.positionals[2]));
    safeErrorPrintLn("'");
    safeErrorPrint("Try 'split --help' for more information.\n");
    return 1;
  }

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"split");
    return 1;
  }

  return run(*cfg_result);
}
