/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implemention for cut.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CUT_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "--bytes", "select only these bytes", STRING_TYPE),
    // [GNU]
    OPTION("-c", "--characters", "select only these characters", STRING_TYPE),
    // [GNU]
    OPTION("-d", "--delimiter", "use DELIM instead of TAB for field delimiter",
           STRING_TYPE),
    // [GNU]
    OPTION("-f", "--fields", "select only these fields", STRING_TYPE),
    // [GNU]
    OPTION("", "--complement",
           "complement the set of selected bytes, characters or fields"),
    // [GNU] -n is "(ignored)" in GNU cut (cut.c:159, 549-550). The
    // former WinuxCmd --no-partial semantics and the impl-only
    // -w/-F/-O/-M options were removed by the #1462 audit: GNU
    // rejects those spellings outright.
    OPTION("-n", "", "do not split multibyte characters (ignored)"),
    // [GNU]
    OPTION("-s", "--only-delimited",
           "do not print lines not containing delimiter"),
    // [GNU]
    OPTION("", "--output-delimiter", "use STRING as the output delimiter",
           STRING_TYPE),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

namespace cut_pipeline {
namespace cp = core::pipeline;

struct Range {
  int start;
  int end;  // inclusive, INT_MAX means open-ended
};

enum class Mode { bytes, characters, fields };

struct Config {
  Mode mode = Mode::fields;
  char delimiter = '\t';
  std::string output_delimiter;
  bool output_delimiter_set = false;
  bool complement = false;
  bool only_delimited = false;
  bool zero_terminated = false;
  std::vector<Range> ranges;
  std::vector<std::string> files;
};

// [GNU] The LIST grammar of set-fields.c, as a single scan: an
// element ends on ',', a blank, or end of string (so '1 2' is two
// elements but '1, 2', '1  2', ' 2', and '2 ' each contain an empty
// element and fail); '-' opens a range; all diagnostics are the
// FATAL_ERROR texts of set-fields.c (verified against the 9.4
// binary, audit #1462). Values are uintmax in GNU; they clamp to
// INT_MAX here, which no real input can distinguish.
auto parse_fields(std::string_view list, Mode mode)
    -> cp::Result<std::vector<Range>> {
  const bool use_pos = mode != Mode::fields;
  auto numbered_error = [&]() -> std::string {
    return use_pos ? "byte/character positions are numbered from 1"
                   : "fields are numbered from 1";
  };
  auto clamp = [](uint64_t v) {
    return v > static_cast<uint64_t>(std::numeric_limits<int>::max())
               ? std::numeric_limits<int>::max()
               : static_cast<int>(v);
  };

  std::vector<Range> ranges;
  uint64_t initial = 1;
  uint64_t value = 0;
  bool lhs_specified = false;
  bool rhs_specified = false;
  bool dash_found = false;
  bool in_digits = false;
  size_t num_start = 0;

  size_t i = 0;
  while (true) {
    const char c = i < list.size() ? list[i] : '\0';
    if (c == '-') {
      in_digits = false;
      if (dash_found) {
        return std::unexpected(use_pos ? "invalid byte or character range"
                                       : "invalid field range");
      }
      dash_found = true;
      ++i;
      if (lhs_specified && !value) {
        return std::unexpected(numbered_error());
      }
      initial = lhs_specified ? value : 1;
      value = 0;
    } else if (c == ',' || c == ' ' || c == '\t' || c == '\0') {
      in_digits = false;
      if (dash_found) {
        dash_found = false;
        if (!lhs_specified && !rhs_specified) {
          return std::unexpected("invalid range with no endpoint: -");
        }
        if (!rhs_specified) {
          ranges.push_back(Range{clamp(initial),
                                 std::numeric_limits<int>::max()});
        } else {
          if (value < initial) {
            return std::unexpected("invalid decreasing range");
          }
          ranges.push_back(Range{clamp(initial), clamp(value)});
        }
        value = 0;
      } else {
        if (value == 0) {
          return std::unexpected(numbered_error());
        }
        ranges.push_back(Range{clamp(value), clamp(value)});
        value = 0;
      }
      if (c == '\0') break;
      ++i;
      lhs_specified = false;
      rhs_specified = false;
    } else if (c >= '0' && c <= '9') {
      if (!in_digits) num_start = i;
      in_digits = true;
      if (dash_found) {
        rhs_specified = true;
      } else {
        lhs_specified = true;
      }
      const uint64_t digit = static_cast<uint64_t>(c - '0');
      if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10 ||
          value * 10 + digit == std::numeric_limits<uint64_t>::max()) {
        size_t run_end = num_start;
        while (run_end < list.size() && list[run_end] >= '0' &&
               list[run_end] <= '9') {
          ++run_end;
        }
        const std::string bad_num(list.substr(num_start, run_end - num_start));
        return std::unexpected(use_pos ? "byte/character offset " + bad_num +
                                             " is too large"
                                       : "field number " + bad_num +
                                             " is too large");
      }
      value = value * 10 + digit;
      ++i;
    } else {
      // GNU quotes the rest of the string from the offending byte.
      const std::string rest(list.substr(i));
      return std::unexpected(use_pos ? "invalid byte/character position '" +
                                           rest + "'"
                                     : "invalid field value '" + rest + "'");
    }
  }
  return ranges;
}

auto is_selected(int index, const std::vector<Range>& ranges) -> bool {
  for (const auto& r : ranges) {
    if (index >= r.start && index <= r.end) return true;
  }
  return false;
}

auto cut_input_open_error(std::string_view input_path) -> std::string {
  // [GNU] "cut: <file>: <errno text>" (cut.c:470-478) (audit #1462).
  std::error_code ec;
  if (std::filesystem::is_directory(
          std::filesystem::u8path(
              native_path::normalize_api_operand(input_path)),
          ec) &&
      !ec) {
    return std::string(input_path) + ": Is a directory";
  }

  return std::string(input_path) + ": No such file or directory";
}

// Bytes of pending output allowed to accumulate before a flush. Output is
// emitted in bounded chunks as records are processed, so downstream
// consumers (e.g. `head`) see data immediately and a closed pipe is
// detected while the producer may still be streaming input.
constexpr size_t kOutputFlushThreshold = 64 * 1024;

// Returns the process exit code when output can no longer continue, or
// std::nullopt while streaming may proceed. A closed downstream pipe is a
// quiet exit like GNU's SIGPIPE death; any other stdout write failure is a
// fatal "write error" diagnostic with exit code 1.
auto stop_status() -> std::optional<int> {
  if (is_stdout_pipe_closed()) return 0;
  if (is_stdout_write_failed()) {
    safeErrorPrintLn("cut: write error");
    return 1;
  }
  return std::nullopt;
}

// Emit buffered output through safePrint so daemon capture handles and
// console conversion keep working, then classify the write result.
auto flush_output(std::string& output) -> std::optional<int> {
  if (!output.empty()) {
    safePrint(std::string_view(output.data(), output.size()));
    output.clear();
  }
  return stop_status();
}

auto build_config(const CommandContext<CUT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  const bool delimiter_specified =
      ctx.has("--delimiter") || ctx.has("-d");
  if (delimiter_specified) {
    std::string delim = ctx.get<std::string>("--delimiter", "");
    if (delim.empty()) delim = ctx.get<std::string>("-d", "");
    if (delim.size() > 1) {
      return std::unexpected("the delimiter must be a single character");
    }
    cfg.delimiter = delim.empty() ? '\0' : delim[0];
  }
  cfg.output_delimiter.clear();
  std::string out_delim = ctx.get<std::string>("--output-delimiter", "");
  cfg.output_delimiter_set = ctx.has("--output-delimiter");
  if (cfg.output_delimiter_set) {
    cfg.output_delimiter = out_delim.empty() ? std::string(1, '\0') : out_delim;
  }

  cfg.only_delimited =
      ctx.get<bool>("--only-delimited", false) || ctx.get<bool>("-s", false);
  cfg.complement = ctx.get<bool>("--complement", false);
  // [GNU] -n is ignored entirely (cut.c:549-550).
  cfg.zero_terminated =
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false);

  // [GNU] ANY second list option -- even a repetition of the same
  // one, even with an empty value -- is fatal (cut.c:515-527), and
  // presence, not a non-empty value, is what counts (audit #1462).
  const size_t list_count =
      ctx.get_all<std::string>("--bytes").size() +
      ctx.get_all<std::string>("-b").size() +
      ctx.get_all<std::string>("--characters").size() +
      ctx.get_all<std::string>("-c").size() +
      ctx.get_all<std::string>("--fields").size() +
      ctx.get_all<std::string>("-f").size();
  if (list_count == 0) {
    return std::unexpected(
        "you must specify a list of bytes, characters, or fields");
  }
  if (list_count > 1) {
    return std::unexpected("only one list may be specified");
  }

  std::string bytes = ctx.get<std::string>("--bytes", "");
  if (bytes.empty() && ctx.has("-b")) bytes = ctx.get<std::string>("-b", "");
  std::string characters = ctx.get<std::string>("--characters", "");
  if (characters.empty() && ctx.has("-c")) {
    characters = ctx.get<std::string>("-c", "");
  }
  std::string fields = ctx.get<std::string>("--fields", "");
  if (fields.empty() && ctx.has("-f")) {
    fields = ctx.get<std::string>("-f", "");
  }

  std::string_view list = fields;
  if (ctx.has("--bytes") || ctx.has("-b")) {
    cfg.mode = Mode::bytes;
    list = bytes;
  } else if (ctx.has("--characters") || ctx.has("-c")) {
    cfg.mode = Mode::characters;
    list = characters;
  } else {
    cfg.mode = Mode::fields;
    list = fields;
  }
  // [GNU] -d and -s have distinct diagnostics in non-field modes
  // (cut.c:563-569); the old code conflated them.
  if (cfg.mode != Mode::fields && delimiter_specified) {
    return std::unexpected(
        "an input delimiter may be specified only when operating on "
        "fields");
  }
  if (cfg.mode != Mode::fields && cfg.only_delimited) {
    return std::unexpected(
        "suppressing non-delimited lines makes sense only when "
        "operating on fields");
  }

  auto ranges = parse_fields(list, cfg.mode);
  if (!ranges) return std::unexpected(ranges.error());
  cfg.ranges = *ranges;

  for (auto p : ctx.positionals) {
    std::string file_arg(p);
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
  if (cfg.files.empty()) cfg.files.emplace_back("-");

  return cfg;
}

auto cut_fields(std::string_view line, const Config& cfg) -> std::string {
  std::vector<std::string_view> fields;
  bool has_delim = false;
  {
    size_t start = 0;
    for (size_t i = 0; i < line.size(); ++i) {
      if (line[i] == cfg.delimiter) {
        has_delim = true;
        fields.emplace_back(line.substr(start, i - start));
        start = i + 1;
      }
    }
    fields.emplace_back(line.substr(start));
  }

  if (!has_delim) {
    if (cfg.only_delimited) return {};
    return std::string(line);
  }

  std::string out;
  bool first = true;
  std::string_view delimiter =
      cfg.output_delimiter_set ? std::string_view(cfg.output_delimiter)
                               : std::string_view(&cfg.delimiter, 1);
  for (int idx = 1; idx <= static_cast<int>(fields.size()); ++idx) {
    bool selected = is_selected(idx, cfg.ranges);
    if (cfg.complement) selected = !selected;
    if (selected) {
      if (!first) out.append(delimiter);
      out.append(fields[idx - 1]);
      first = false;
    }
  }
  return out;
}

auto cut_bytes_or_characters(std::string_view line, const Config& cfg)
    -> std::string {
  std::string out;
  bool previous_selected = false;
  bool wrote_any = false;

  for (size_t offset = 0; offset < line.size();) {
    // [GNU] -n is ignored: selection is purely per byte.
    size_t end = offset + 1;
    size_t first_byte = offset + 1;
    bool selected = is_selected(static_cast<int>(first_byte), cfg.ranges);
    if (cfg.complement) selected = !selected;
    if (!selected) {
      previous_selected = false;
      offset = end;
      continue;
    }

    if (wrote_any && !previous_selected && cfg.output_delimiter_set) {
      out.append(cfg.output_delimiter);
    }
    out.append(line.substr(offset, end - offset));
    wrote_any = true;
    previous_selected = true;
    offset = end;
  }

  return out;
}

auto cut_line(std::string_view line, const Config& cfg) -> std::string {
  if (cfg.mode == Mode::fields) return cut_fields(line, cfg);
  return cut_bytes_or_characters(line, cfg);
}
auto record_has_field_delimiter(std::string_view rec, const Config& cfg)
    -> bool {
  return rec.find(cfg.delimiter) != std::string::npos;
}

auto can_use_fast_field_stream(const Config& cfg) -> bool {
  const char record_delim = cfg.zero_terminated ? '\0' : '\n';
  return cfg.mode == Mode::fields && !cfg.complement &&
         !cfg.output_delimiter_set && cfg.delimiter != record_delim;
}

auto append_fast_field_record(std::string_view rec, const Config& cfg,
                              std::string& output) -> bool {
  if (rec.find(cfg.delimiter) == std::string_view::npos) {
    if (cfg.only_delimited) return false;
    output.append(rec);
    return true;
  }
  bool has_delim = false;
  bool wrote_selected = false;
  int field_index = 1;
  size_t field_start = 0;

  for (size_t i = 0; i <= rec.size(); ++i) {
    if (i != rec.size() && rec[i] != cfg.delimiter) continue;

    if (i != rec.size()) has_delim = true;
    const bool selected = is_selected(field_index, cfg.ranges);
    if (selected) {
      if (wrote_selected) output.push_back(cfg.delimiter);
      output.append(rec.substr(field_start, i - field_start));
      wrote_selected = true;
    }

    ++field_index;
    field_start = i + 1;
  }

  return true;
}

// Stream one record at a time: cut never holds more than the current
// record plus a bounded output buffer, so `yes | cut -c1- | head -1` emits
// immediately and exits quietly once the downstream pipe closes.
auto run_file(const std::string& path, const Config& cfg) -> int {
  std::ifstream file;
  std::istream* input = nullptr;
  if (path == "-") {
    // [GNU] closed stdin (<&-) errors "cut: -: Bad file descriptor".
    if (file_io::stdin_is_bad()) {
      cp::report_custom_error(L"cut", L"-: Bad file descriptor");
      return 1;
    }
#ifdef _WIN32
    // Binary mode keeps '\r', NUL records and 0x1A bytes intact so the
    // record bytes reach the field/byte logic unchanged (GNU keeps CR).
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    input = &std::cin;
  } else {
    file.open(native_path::normalize_api_operand(path), std::ios::binary);
    if (!file.is_open()) {
      cp::report_custom_error(L"cut",
                              utf8_to_wstring(cut_input_open_error(path)));
      return 1;
    }
    input = &file;
  }

  const char record_delim = cfg.zero_terminated ? '\0' : '\n';
  const bool fast_fields = can_use_fast_field_stream(cfg);
  std::string record;
  std::string output;
  output.reserve(kOutputFlushThreshold);

  while (std::getline(*input, record, record_delim)) {
    bool should_emit = true;
    if (fast_fields) {
      should_emit = append_fast_field_record(record, cfg, output);
    } else {
      std::string out = cut_line(record, cfg);
      if (out.empty() && cfg.only_delimited &&
          !record_has_field_delimiter(record, cfg)) {
        should_emit = false;
      } else {
        output.append(out);
      }
    }
    if (!should_emit) continue;
    output.push_back(record_delim);

    if (output.size() >= kOutputFlushThreshold) {
      if (auto rc = flush_output(output)) return *rc;
    }
  }

  if (auto rc = flush_output(output)) return *rc;
  return 0;
}

auto run(const Config& cfg) -> int {
  // [GNU] ok accumulates over ALL operands (cut.c:595-596): a bad
  // file is diagnosed and later files are still processed; the
  // exit status is 1 at the end (audit #1462).
  int exit_code = 0;
  for (const auto& f : cfg.files) {
    int rc = run_file(f, cfg);
    // A dead stdout ends the pipeline: remaining files would only hit the
    // same failed writer (GNU dies on SIGPIPE mid-stream too).
    if (auto stop = stop_status()) return *stop;
    if (rc != 0) exit_code = 1;
  }
  return exit_code;
}

}  // namespace cut_pipeline

REGISTER_COMMAND(cut, "cut", "cut OPTION... [FILE]...",
                 "Print selected parts of lines from each FILE to standard "
                 "output.",
                 "  cut -f1,3 file.txt\n"
                 "  cut -d, -f2 data.csv\n"
                 "  cut -z -f1 -d: list.txt",
                 "paste(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 CUT_OPTIONS) {
  using namespace cut_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    // [GNU] cut's fatal errors all end with the Try line
    // (FATAL_ERROR, cut.c:41-47).
    cp::report_error(cfg, L"cut");
    safeErrorPrintLn("Try 'cut --help' for more information.");
    return 1;
  }

  return run(*cfg);
}
