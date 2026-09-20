// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for comm.
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

auto constexpr COMM_OPTIONS = std::array{
    // [GNU]
    OPTION("-1", "", "suppress column 1 (lines unique to FILE1)", BOOL_TYPE),
    // [GNU]
    OPTION("-2", "", "suppress column 2 (lines unique to FILE2)", BOOL_TYPE),
    // [GNU]
    OPTION("-3", "", "suppress column 3 (lines that appear in both files)",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--check-order", "check that the input is correctly sorted",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--nocheck-order",
           "do not check that the input is correctly sorted", BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    // [GNU]
    OPTION("", "--output-delimiter", "separate columns with STR",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("", "--total", "output a summary")};

namespace comm_pipeline {
namespace cp = core::pipeline;

enum class OrderCheckMode { Default, Enabled, Disabled };

struct Config {
  bool suppress_col1 = false;
  bool suppress_col2 = false;
  bool suppress_col3 = false;
  OrderCheckMode order_check = OrderCheckMode::Default;
  bool total = false;
  bool zero_terminated = false;
  std::string output_delimiter = "\t";
  SmallVector<std::string, 64> files;
};

void add_file_arg(Config& cfg, const std::string& file_arg) {
  if (contains_wildcard(file_arg)) {
    auto glob_result = glob_expand(file_arg);
    if (glob_result.expanded) {
      for (const auto& file : glob_result.files) {
        cfg.files.push_back(wstring_to_utf8(file));
      }
      return;
    }
  }
  cfg.files.push_back(file_arg);
}

auto build_config(const CommandContext<COMM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  bool output_delimiter_seen = false;
  auto set_output_delimiter = [&](std::string value) -> cp::Result<void> {
    std::string normalized =
        value.empty() ? std::string(1, static_cast<char>(0)) : std::move(value);
    if (output_delimiter_seen && cfg.output_delimiter != normalized) {
      return std::unexpected("multiple output delimiters specified");
    }
    cfg.output_delimiter = std::move(normalized);
    output_delimiter_seen = true;
    return {};
  };

  bool end_of_options = false;
  for (size_t i = 0; i < ctx.raw_args.size(); ++i) {
    std::string arg(ctx.raw_args[i]);

    if (!end_of_options && arg == "--") {
      end_of_options = true;
      continue;
    }

    if (!end_of_options && arg.starts_with("--output-delimiter=")) {
      std::string value = arg.substr(std::string("--output-delimiter=").size());
      auto delimiter_result = set_output_delimiter(std::move(value));
      if (!delimiter_result) return std::unexpected(delimiter_result.error());
      continue;
    }

    if (!end_of_options && arg == "--output-delimiter") {
      if (i + 1 >= ctx.raw_args.size()) {
        return std::unexpected(
            "option --output-delimiter requires an argument");
      }
      auto delimiter_result =
          set_output_delimiter(std::string(ctx.raw_args[++i]));
      if (!delimiter_result) return std::unexpected(delimiter_result.error());
      continue;
    }

    if (!end_of_options && arg.starts_with("--")) {
      if (arg == "--check-order") {
        cfg.order_check = OrderCheckMode::Enabled;
        continue;
      }
      if (arg == "--nocheck-order") {
        cfg.order_check = OrderCheckMode::Disabled;
        continue;
      }
      if (arg == "--total") {
        cfg.total = true;
        continue;
      }
      if (arg == "--zero-terminated") {
        cfg.zero_terminated = true;
        continue;
      }
      // Unknown long option
      return std::unexpected("invalid option -- '" + arg + "'");
    }

    if (!end_of_options && arg.size() >= 2 && arg[0] == '-' && arg[1] != '-') {
      bool all_flags = true;
      for (size_t pos = 1; pos < arg.size(); ++pos) {
        switch (arg[pos]) {
          case '1':
            cfg.suppress_col1 = true;
            break;
          case '2':
            cfg.suppress_col2 = true;
            break;
          case '3':
            cfg.suppress_col3 = true;
            break;
          case 'z':
            cfg.zero_terminated = true;
            break;
          default:
            all_flags = false;
            break;
        }
        if (!all_flags) break;
      }
      if (all_flags) continue;
      // Unknown short option
      return std::unexpected("invalid option -- '" + std::string(1, arg[1]) +
                             "'");
    }

    add_file_arg(cfg, arg);
  }

  // Keep these diagnostics as English fallbacks here; they are routed
  // through i18n::translate_error / i18n::format at the print site so the
  // handler can still recognize them in any locale.
  if (cfg.files.empty()) {
    return std::unexpected(std::string("missing operand"));
  }
  if (cfg.files.size() < 2) {
    // [GNU] comm.c: "missing operand after %s" quotes argv[argc - 1] after
    // getopt permutation, i.e. the last operand.
    return std::unexpected("missing operand after '" + cfg.files.back() + "'");
  }
  if (cfg.files.size() > 2) {
    return std::unexpected("extra operand '" + cfg.files[2] + "'");
  }

  return cfg;
}

// Read sorted lines from file
auto read_lines(const std::string& filename, char delimiter)
    -> cp::Result<SmallVector<std::string, 1024>> {
  SmallVector<std::string, 1024> lines;
  auto normalize_line = [delimiter](std::string& line) {
    if (delimiter == '\n' && !line.empty() && line.back() == '\r') {
      line.pop_back();
    }
  };

  if (filename == "-") {
    // Read from stdin
    std::string content;
    {
      std::ostringstream oss;
      oss << std::cin.rdbuf();
      content = oss.str();
    }
    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        std::string line = content.substr(start, i - start);
        normalize_line(line);
        lines.emplace_back(std::move(line));
        start = i + 1;
      }
    }
    if (start < content.size()) {
      std::string line = content.substr(start);
      normalize_line(line);
      lines.emplace_back(std::move(line));
    }
  } else {
    auto comm_input_open_error = [](std::string_view path) -> std::string {
      std::error_code ec;
      auto status = std::filesystem::status(std::filesystem::u8path(path), ec);
      if (!ec && status.type() == std::filesystem::file_type::directory) {
        return std::string("cannot open '") + std::string(path) +
               "' for reading: Is a directory";
      }
      return std::string("cannot open '") + std::string(path) +
             "' for reading: No such file or directory";
    };

    // Read from file
    std::ifstream f(native_path::normalize_api_operand(filename),
                    std::ios::binary);
    if (!f) {
      return std::unexpected(comm_input_open_error(filename));
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        std::string line = content.substr(start, i - start);
        // Skip UTF-8 BOM if present at the beginning of the first line
        if (lines.empty() && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
          line = line.substr(3);
        }
        normalize_line(line);
        lines.push_back(line);
        start = i + 1;
      }
    }
    if (start < content.size()) {
      std::string line = content.substr(start);
      if (lines.empty() && line.size() >= 3 &&
          static_cast<unsigned char>(line[0]) == 0xEF &&
          static_cast<unsigned char>(line[1]) == 0xBB &&
          static_cast<unsigned char>(line[2]) == 0xBF) {
        line = line.substr(3);
      }
      normalize_line(line);
      lines.push_back(line);
    }

    if (f.fail() && !f.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  return lines;
}

void report_disorder(int file_number) {
  // [GNU] "comm: file N is not in sorted order"
  safeErrorPrintLn(winux::i18n::format("command.comm.error.file_not_sorted",
                                       "comm: file {} is not in sorted order",
                                       std::to_string(file_number)));
}

void print_record(const std::string& text, char delimiter) {
  safePrint(text);
  safePrint(std::string_view(&delimiter, 1));
}

void append_column_prefix(std::string& output, const Config& cfg, int column) {
  if (column >= 2 && !cfg.suppress_col1) {
    output += cfg.output_delimiter;
  }
  if (column >= 3 && !cfg.suppress_col2) {
    output += cfg.output_delimiter;
  }
}

auto run(const Config& cfg) -> int {
  const char record_delim = cfg.zero_terminated ? '\0' : '\n';
  const std::string& file1 = cfg.files[0];
  const std::string& file2 = cfg.files[1];

  // [GNU] comm.c: both operands "-" share the stdin stream; the first read
  // consumes it and the second sees EOF, so every line lands in column 1.
  auto lines1_result = read_lines(file1, record_delim);
  if (!lines1_result) {
    cp::report_error(lines1_result, L"comm");
    return 1;
  }

  auto lines2_result = read_lines(file2, record_delim);
  if (!lines2_result) {
    cp::report_error(lines2_result, L"comm");
    return 1;
  }

  const auto& lines1 = *lines1_result;
  const auto& lines2 = *lines2_result;
  const size_t n1 = lines1.size();
  const size_t n2 = lines2.size();

  const bool check_enabled = cfg.order_check == OrderCheckMode::Enabled;
  const bool check_disabled = cfg.order_check == OrderCheckMode::Disabled;
  bool seen_unpairable = false;
  bool issued_disorder[2] = {false, false};

  // [GNU] comm.c check_order(): the line at CURRENT is compared with its
  // predecessor in the same file.  With --check-order disorder is fatal
  // (the offending line is never emitted, but output already produced is
  // kept); in the default mode the warning is issued at most once per file
  // and only once an unpairable line has been seen.  Returns true when the
  // merge must stop.
  auto check_order = [&](int which, size_t current) -> bool {
    const auto& lines = which == 1 ? lines1 : lines2;
    if (!check_disabled && (check_enabled || seen_unpairable) &&
        !issued_disorder[which - 1] && lines[current - 1] > lines[current]) {
      issued_disorder[which - 1] = true;
      report_disorder(which);
      return check_enabled;
    }
    return false;
  };

  // Advance file WHICH past its just-consumed line, mirroring GNU comm.c
  // fill_up: the newly read line is order-checked, and at end of file the
  // last line pair is re-checked because seen_unpairable may have become
  // true after that pair's earlier (silent) check.
  auto advance = [&](int which, size_t& cursor, size_t total) -> bool {
    ++cursor;
    if (cursor < total) return check_order(which, cursor);
    if (total >= 2) return check_order(which, total - 1);
    return false;
  };

  // Merge and compare
  size_t i = 0, j = 0;
  size_t count_col1 = 0, count_col2 = 0, count_col3 = 0;
  while (i < n1 || j < n2) {
    int cmp_result = 0;

    if (i >= n1) {
      cmp_result = 1;
    } else if (j >= n2) {
      cmp_result = -1;
    } else {
      cmp_result = lines1[i].compare(lines2[j]);
    }

    if (cmp_result == 0) {
      // Line in both files
      ++count_col3;
      if (!cfg.suppress_col3) {
        std::string output;
        append_column_prefix(output, cfg, 3);
        output += lines1[i];
        print_record(output, record_delim);
      }
    } else {
      seen_unpairable = true;
      if (cmp_result < 0) {
        // Line only in file1
        ++count_col1;
        if (!cfg.suppress_col1) {
          print_record(lines1[i], record_delim);
        }
      } else {
        // Line only in file2
        ++count_col2;
        if (!cfg.suppress_col2) {
          std::string output;
          append_column_prefix(output, cfg, 2);
          output += lines2[j];
          print_record(output, record_delim);
        }
      }
    }

    if (cmp_result <= 0 && advance(1, i, n1)) return 1;
    if (cmp_result >= 0 && advance(2, j, n2)) return 1;
  }

  if (cfg.total) {
    std::string output = std::to_string(count_col1);
    output += cfg.output_delimiter;
    output += std::to_string(count_col2);
    output += cfg.output_delimiter;
    output += std::to_string(count_col3);
    output += cfg.output_delimiter;
    output += "total";
    print_record(output, record_delim);
  }

  if (issued_disorder[0] || issued_disorder[1]) {
    // [GNU] "comm: input is not in sorted order"
    safeErrorPrintLn(winux::i18n::translate(
        "command.comm.error.not_sorted", "comm: input is not in sorted order"));
    return 1;
  }

  return 0;
}

}  // namespace comm_pipeline

REGISTER_COMMAND(
    comm, "comm", "comm [OPTION]... FILE1 FILE2",
    "Compare sorted files line by line.\n"
    "\n"
    "With no options, produce three-column output. Column one contains\n"
    "lines unique to FILE1, column two contains lines unique to FILE2,\n"
    "and column three contains lines common to both files.\n"
    "\n"
    "Note: Input files must be sorted. This implementation does not\n"
    "automatically sort the input files.",
    "  comm file1 file2\n"
    "  comm -12 file1 file2        # show only common lines\n"
    "  comm -23 file1 file2        # show only lines in file1\n"
    "  comm -13 file1 file2        # show only lines in file2\n"
    "  comm -3 file1 file2 | wc -l # count common lines",
    "cmp(1), diff(1), uniq(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    COMM_OPTIONS) {
  using namespace comm_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error().starts_with("missing operand")) {
      // [GNU] "comm: missing operand" / "comm: missing operand after 'FILE'"
      safeErrorPrint("comm: ");
      safeErrorPrintLn(winux::i18n::translate_error(cfg_result.error()));
      safeErrorPrintLn(winux::i18n::format(
          "common.try_help", "Try '{} --help' for more information.", "comm"));
      return 1;
    }
    if (cfg_result.error().starts_with("extra operand '")) {
      safeErrorPrint("comm: ");
      safeErrorPrintLn(winux::i18n::translate_error(cfg_result.error()));
      safeErrorPrintLn(winux::i18n::format(
          "common.try_help", "Try '{} --help' for more information.", "comm"));
      return 1;
    }
    cp::report_error(cfg_result, L"comm");
    return 1;
  }

  return run(*cfg_result);
}
