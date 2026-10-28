// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for xargs.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;
import container;

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief XARGS command options definition
 *
 * This array defines all the options supported by the xargs command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -n, --max-args: Use at most max-args arguments per command line
 * [IMPLEMENTED]
 * - @a -I: Replace occurrences of replace-str in the initial-arguments with
 * names [IMPLEMENTED]
 * - @a -i, --replace: Deprecated alias for -I [IMPLEMENTED]
 * - @a -0, --null: Input items are terminated by a null character [IMPLEMENTED]
 * - @a -d, --delimiter: Input items are terminated by the specified character
 * [IMPLEMENTED]
 * - @a -o, --open-tty: Reopen standard input as the console for children
 * [IMPLEMENTED ON WINDOWS CONSOLE]
 * - @a -t, --verbose: Print the command line on the standard error before
 * executing it [IMPLEMENTED]
 * - @a -p, --interactive: Prompt before running each command line
 * [IMPLEMENTED]
 * - @a -r, --no-run-if-empty: If the standard input is completely empty, do
 * not run the command [IMPLEMENTED]
 * - @a -P, --max-procs: Run up to max-procs processes at a time [IMPLEMENTED]
 * - @a -s, --max-chars: Use at most max-chars chars per command line
 * [IMPLEMENTED]
 * - @a -x, --exit: Exit if the size (see -s) is exceeded [IMPLEMENTED]
 * - @a -L, --max-lines: Use at most max-lines nonblank input lines per
 * command line [IMPLEMENTED]
 * - @a -a, --arg-file: Read items from file instead of standard input
 * [IMPLEMENTED]
 * - @a -E: Set the logical EOF string [IMPLEMENTED]
 * - @a -e, --eof: Set or disable the logical EOF string [IMPLEMENTED]
 * - @a --show-limits: Display command-line length limits [IMPLEMENTED]
 * - @a --process-slot-var: Set an environment variable to the process slot
 * [IMPLEMENTED]
 */
auto constexpr XARGS_OPTIONS = std::array{
    // [GNU] option
    OPTION("-n", "--max-args",
           "use at most max-args arguments per command line", INT_TYPE),
    // [GNU] option
    OPTION("-I", "",
           "replace occurrences of replace-str in the initial-arguments with "
           "names",
           STRING_TYPE),
    // [GNU] option
    OPTION("-i", "--replace",
           "deprecated alias for -I; replace occurrences of replace-str",
           OPTIONAL_STRING_TYPE),
    // [GNU] option
    OPTION("-0", "--null", "input items are terminated by a null character"),
    // [GNU] option
    OPTION("-d", "--delimiter",
           "input items are terminated by the specified character",
           STRING_TYPE),
    // [GNU] option
    OPTION("-o", "--open-tty",
           "reopen standard input as the console in the child process"),
    // [GNU] option
    OPTION("-t", "--verbose",
           "print the command line on the standard error before executing it"),
    // [GNU] option
    OPTION("-p", "--interactive", "prompt before running each command line"),
    // [GNU] option (xargs.c: "-r, --no-run-if-empty"; findutils runs nothing
    // whenever no arguments were accumulated, not merely when the raw input
    // stream was byte-empty)
    OPTION("-r", "--no-run-if-empty",
           "if no arguments were read from standard input, do not run the "
           "command"),
    // [GNU] option
    OPTION("-P", "--max-procs", "run up to max-procs processes at a time",
           INT_TYPE),
    // [GNU] option
    OPTION("-s", "--max-chars", "use at most max-chars chars per command line",
           INT_TYPE),
    // [GNU] option
    OPTION("-x", "--exit", "exit if the size (see -s) is exceeded"),
    // [GNU] option
    OPTION("-L", "",
           "use at most max-lines nonblank input lines per command line",
           INT_TYPE),
    // [GNU] option
    OPTION("", "--max-lines",
           "use at most max-lines nonblank input lines per command line",
           OPTIONAL_INT_TYPE),
    // [GNU] option
    OPTION("-l", "",
           "deprecated alias for -L; use max-lines nonblank input lines",
           OPTIONAL_INT_TYPE),
    // [GNU] option
    OPTION("-a", "--arg-file", "read items from file instead of standard input",
           STRING_TYPE),
    // [GNU] option
    OPTION("-E", "", "set the logical EOF string", STRING_TYPE),
    // [GNU] option
    OPTION("", "--eof", "set or disable the logical EOF string",
           OPTIONAL_STRING_TYPE),
    // [GNU] option
    OPTION("-e", "", "deprecated alias for --eof", OPTIONAL_STRING_TYPE),
    // [GNU] option
    OPTION("", "--show-limits", "display command-line length limits"),
    // [GNU] option
    OPTION("", "--process-slot-var",
           "set an environment variable to the child process slot",
           STRING_TYPE)};

namespace xargs_pipeline {
namespace cp = core::pipeline;

constexpr int kWindowsCommandLineLimit = 32767;

template <size_t N>
auto option_present(const CommandContext<N> &ctx, std::string_view name)
    -> bool {
  if (!ctx.metas) return false;

  for (size_t i = 0; i < N; ++i) {
    if ((*ctx.metas)[i].long_name == name ||
        (*ctx.metas)[i].short_name == name) {
      return ctx.options.has(i);
    }
  }
  return false;
}

template <typename T, size_t N>
auto last_option_value(const CommandContext<N> &ctx,
                       std::string_view short_name, std::string_view long_name,
                       T fallback) -> T {
  T value = fallback;
  if (!ctx.metas) return value;

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= N) continue;
    const auto &meta = (*ctx.metas)[occurrence.index];
    if (!option_matches(meta, short_name, long_name)) continue;
    if (const auto *parsed = std::get_if<T>(&occurrence.value)) value = *parsed;
  }
  return value;
}

enum class BatchOptionFamily {
  None,
  MaxArgs,
  MaxLines,
  Replace,
};

struct BatchOptions {
  int max_args = 0;
  int max_lines = 0;
  std::string replace_str;
  BatchOptionFamily active_family = BatchOptionFamily::None;
  bool max_args_present = false;
  bool max_lines_present = false;
};

auto batch_option_family_name(BatchOptionFamily family) -> std::string_view {
  switch (family) {
    case BatchOptionFamily::MaxArgs:
      return "--max-args";
    case BatchOptionFamily::MaxLines:
      return "--max-lines";
    case BatchOptionFamily::Replace:
      return "--replace";
    case BatchOptionFamily::None:
      break;
  }
  return "";
}

auto warn_conflicting_batch_options(BatchOptionFamily previous_family,
                                    BatchOptionFamily next_family) -> void {
  if (previous_family == BatchOptionFamily::None ||
      previous_family == next_family) {
    return;
  }

  if (previous_family == BatchOptionFamily::MaxLines &&
      next_family == BatchOptionFamily::MaxArgs) {
    safeErrorPrint(
        "xargs: warning: options --max-lines and --max-args/-n "
        "are mutually exclusive, ignoring previous --max-lines "
        "value\n");
    return;
  }

  if (previous_family == BatchOptionFamily::MaxArgs &&
      next_family == BatchOptionFamily::MaxLines) {
    safeErrorPrint(
        "xargs: warning: options --max-args and "
        "--max-lines/-L/-l are mutually exclusive, ignoring "
        "previous --max-args value\n");
    return;
  }

  if (previous_family == BatchOptionFamily::MaxArgs &&
      next_family == BatchOptionFamily::Replace) {
    safeErrorPrint(
        "xargs: warning: options --max-args and "
        "--replace/-I/-i are mutually exclusive, ignoring "
        "previous --max-args value\n");
    return;
  }

  if (previous_family == BatchOptionFamily::Replace &&
      next_family == BatchOptionFamily::MaxArgs) {
    safeErrorPrint(
        "xargs: warning: options --replace/-I/-i and "
        "--max-args/-n are mutually exclusive, ignoring "
        "previous --replace value\n");
    return;
  }

  if (previous_family == BatchOptionFamily::MaxLines &&
      next_family == BatchOptionFamily::Replace) {
    safeErrorPrint(
        "xargs: warning: options --max-lines and "
        "--replace/-I/-i are mutually exclusive, ignoring "
        "previous --max-lines value\n");
    return;
  }

  if (previous_family == BatchOptionFamily::Replace &&
      next_family == BatchOptionFamily::MaxLines) {
    safeErrorPrint(
        "xargs: warning: options --replace/-I/-i and "
        "--max-lines/-L/-l are mutually exclusive, ignoring "
        "previous --replace value\n");
    return;
  }

  safeErrorPrint("xargs: warning: options ");
  safeErrorPrint(std::string(batch_option_family_name(previous_family)));
  safeErrorPrint(" and ");
  safeErrorPrint(std::string(batch_option_family_name(next_family)));
  safeErrorPrint(" are mutually exclusive, ignoring previous ");
  safeErrorPrint(std::string(batch_option_family_name(previous_family)));
  safeErrorPrint(" value\n");
}

auto normalize_batch_options(const CommandContext<XARGS_OPTIONS.size()> &ctx)
    -> BatchOptions {
  BatchOptions options;

  auto clear_active = [&]() {
    options.max_args = 0;
    options.max_lines = 0;
    options.replace_str.clear();
    options.max_args_present = false;
    options.max_lines_present = false;
  };

  auto switch_family = [&](BatchOptionFamily family) {
    if (options.active_family != BatchOptionFamily::None &&
        options.active_family != family) {
      warn_conflicting_batch_options(options.active_family, family);
      clear_active();
    }
    options.active_family = family;
  };

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= XARGS_OPTIONS.size()) continue;
    const auto &meta = (*ctx.metas)[occurrence.index];

    if (option_matches(meta, "-n", "--max-args")) {
      const auto *value = std::get_if<int>(&occurrence.value);
      if (!value) continue;

      if (options.active_family == BatchOptionFamily::Replace && *value == 1) {
        continue;
      }

      switch_family(BatchOptionFamily::MaxArgs);
      options.max_args = *value;
      options.max_args_present = true;
      continue;
    }

    if (option_matches(meta, "-L", "--max-lines") ||
        option_matches(meta, "-l", "")) {
      const auto *value = std::get_if<int>(&occurrence.value);
      if (!value) continue;

      switch_family(BatchOptionFamily::MaxLines);
      options.max_lines = *value < 0 ? 1 : *value;
      options.max_lines_present = true;
      continue;
    }

    if (option_matches(meta, "-I", "") ||
        option_matches(meta, "-i", "--replace")) {
      const auto *value = std::get_if<std::string>(&occurrence.value);
      if (!value) continue;

      switch_family(BatchOptionFamily::Replace);
      options.replace_str = *value;
      if (options.replace_str.empty() && !option_matches(meta, "-I", "")) {
        options.replace_str = "{}";
      }
    }
  }

  return options;
}

struct DelimiterMode {
  bool use_null = false;
  bool has_delimiter = false;
  std::string delimiter_arg;
};

// #1322: -0 and -d do not have a fixed priority; the last one on the
// command line wins (GNU), and the winner also decides whether the
// logical EOF string and the -E warning apply.
auto resolve_delimiter_mode(const CommandContext<XARGS_OPTIONS.size()> &ctx)
    -> DelimiterMode {
  DelimiterMode mode;
  if (!ctx.metas) return mode;

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= XARGS_OPTIONS.size()) continue;
    const auto &meta = (*ctx.metas)[occurrence.index];
    if (option_matches(meta, "-0", "--null")) {
      mode.use_null = true;
      mode.has_delimiter = false;
      mode.delimiter_arg.clear();
    } else if (option_matches(meta, "-d", "--delimiter")) {
      mode.use_null = false;
      mode.has_delimiter = true;
      if (const auto *value =
              std::get_if<std::string>(&occurrence.value)) {
        mode.delimiter_arg = *value;
      }
    }
  }
  return mode;
}

/**
 * @brief Parse arguments from stdin
 * @param delimiter Delimiter character (default: space/newline)
 * @param replace_str Replacement string for -I option
 * @return Vector of parsed arguments
 */
auto hex_value(char c) -> int {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

auto parse_delimiter(std::string_view text) -> cp::Result<char> {
  if (text.empty()) return std::unexpected("delimiter must not be empty");
  if (text.size() == 1) return text[0];
  if (text[0] != '\\') {
    return std::unexpected("delimiter must be a single character");
  }

  if (text.size() == 2) {
    switch (text[1]) {
      case '0':
        return '\0';
      case 'a':
        return '\a';
      case 'b':
        return '\b';
      case 'f':
        return '\f';
      case 'n':
        return '\n';
      case 'r':
        return '\r';
      case 't':
        return '\t';
      case 'v':
        return '\v';
      case '\\':
        return '\\';
      default:
        if (text[1] == 'x') {
          return std::unexpected("invalid hex delimiter escape");
        }
        return std::unexpected("invalid delimiter escape");
        break;
    }
  }

  if (text.size() >= 3 && text[1] == 'x') {
    int value = 0;
    for (size_t i = 2; i < text.size(); ++i) {
      int digit = hex_value(text[i]);
      if (digit < 0) return std::unexpected("invalid hex delimiter escape");
      value = value * 16 + digit;
      if (value > 255) return std::unexpected("delimiter escape out of range");
    }
    return static_cast<char>(value);
  }

  if (text.size() >= 2 && text[1] >= '0' && text[1] <= '7') {
    int value = 0;
    for (size_t i = 1; i < text.size(); ++i) {
      if (text[i] < '0' || text[i] > '7') {
        return std::unexpected("invalid octal delimiter escape");
      }
      value = value * 8 + (text[i] - '0');
      if (value > 255) return std::unexpected("delimiter escape out of range");
    }
    return static_cast<char>(value);
  }

  return std::unexpected("delimiter must be a single character");
}

auto read_input_text(std::istream &input,
                     const std::optional<std::string> &eof_str) -> std::string {
  std::string text;
  if (!eof_str || eof_str->empty()) {
    text.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    return text;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == *eof_str) break;
    text += line;
    text.push_back('\n');
  }
  return text;
}

auto parse_default_arguments(std::string_view text)
    -> cp::Result<std::vector<std::string>> {
  SmallVector<std::string, 256> args;
  std::string arg;
  bool in_single_quote = false;
  bool in_double_quote = false;
  bool escaped = false;
  bool have_arg = false;

  auto flush_arg = [&]() {
    if (have_arg) {
      args.push_back(arg);
      arg.clear();
      have_arg = false;
    }
  };

  for (char c : text) {
    // #1321: a newline inside either quote kind is an error (GNU has no
    // line continuation inside quotes), even right after a backslash.
    if ((in_single_quote || in_double_quote) && c == '\n') {
      return std::unexpected(
          in_single_quote
              ? "unmatched single quote; by default quotes are special to "
                "xargs unless you use the -0 option"
              : "unmatched double quote; by default quotes are special to "
                "xargs unless you use the -0 option");
    }

    if (escaped) {
      if (in_double_quote) {
        // #1321: inside double quotes only $, `, " and \ are escapable;
        // any other character keeps its backslash literally.
        if (c != '$' && c != '`' && c != '"' && c != '\\') arg += '\\';
        arg += c;
      } else {
        arg += c;
      }
      have_arg = true;
      escaped = false;
      continue;
    }

    if (in_single_quote) {
      if (c == '\'') {
        in_single_quote = false;
      } else {
        arg += c;
      }
      have_arg = true;
      continue;
    }

    if (in_double_quote) {
      if (c == '"') {
        in_double_quote = false;
      } else if (c == '\\') {
        escaped = true;
      } else {
        arg += c;
      }
      have_arg = true;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      have_arg = true;
    } else if (c == '\'') {
      in_single_quote = true;
      have_arg = true;
    } else if (c == '"') {
      in_double_quote = true;
      have_arg = true;
    } else if (c == '\r') {
      // #1321: a CR at token start is skipped; inside a token it is
      // literal (it is not a separator).
      if (have_arg) arg += c;
    } else if (c == '\n' || c == ' ' || c == '\t') {
      flush_arg();
    } else {
      arg += c;
      have_arg = true;
    }
  }

  // #1321: a trailing backslash at EOF is dropped (GNU), not preserved.
  if (in_single_quote) {
    return std::unexpected(
        "unmatched single quote; by default quotes are special to xargs "
        "unless you use the -0 option");
  }
  if (in_double_quote) {
    return std::unexpected(
        "unmatched double quote; by default quotes are special to xargs "
        "unless you use the -0 option");
  }

  flush_arg();
  return std::vector<std::string>(args.begin(), args.end());
}

auto split_blank_arguments(std::string_view line)
    -> cp::Result<std::vector<std::string>> {
  return parse_default_arguments(line);
}

auto parse_arguments(std::istream &input, char delimiter, bool split_blanks,
                     const std::optional<std::string> &eof_str)
    -> cp::Result<std::vector<std::string>> {
  auto text = read_input_text(input, eof_str);
  if (text.empty()) {
    // #1318: empty input yields zero items in every mode.  The loop below
    // used to synthesize one empty item for the delimiter/-0 path (the
    // final `!saw_separator` push), which also defeated -r.
    return std::vector<std::string>{};
  }
  if (split_blanks) {
    return parse_default_arguments(text);
  }

  SmallVector<std::string, 256> args;
  std::string arg;
  bool saw_separator = false;

  for (char c : text) {
    bool is_separator = c == delimiter;

    if (is_separator) {
      args.push_back(arg);
      arg.clear();
      saw_separator = true;
    } else {
      arg += c;
      saw_separator = false;
    }
  }

  // Preserve zero-length items between consecutive explicit delimiters, but
  // do not synthesize a trailing empty item for a final terminator.
  if (!arg.empty() || !saw_separator) {
    args.push_back(arg);
  }

  return std::vector<std::string>(args.begin(), args.end());
}

auto is_blank_input_line(std::string_view value) -> bool {
  // #1321/#1322: a blank line holds only spaces, tabs, or CRs.
  return value.find_first_not_of(" \t\r") == std::string_view::npos;
}

auto parse_replacement_arguments(std::istream &input,
                                 const std::optional<std::string> &eof_str)
    -> cp::Result<std::vector<std::string>> {
  // #1321/#1322: -I mode rewritten to the GNU line model.  The old code
  // copied raw lines (no quote processing at all) and treated a trailing
  // blank as a logical-line continuation; both are wrong.  Now each line
  // is one item: leading blanks are stripped, trailing blanks are kept,
  // quotes/backslashes follow the default-mode rules (without blank
  // splitting), and only a trailing unquoted backslash continues the item
  // onto the next line (the backslash is dropped, the newline kept).
  SmallVector<std::string, 256> args;
  std::string item;
  bool in_single_quote = false;
  bool in_double_quote = false;
  bool escaped = false;
  bool have_item = false;
  bool continuing = false;
  bool pending_newline = false;

  auto unmatched_error = [&]() -> std::string {
    return in_single_quote
               ? "unmatched single quote; by default quotes are special to "
                 "xargs unless you use the -0 option"
               : "unmatched double quote; by default quotes are special to "
                 "xargs unless you use the -0 option";
  };

  // Feed one physical line into the current item.  Returns an error string
  // on an unclosed quote, or std::nullopt on success.  `strip_leading`
  // applies only to the first line of an item.
  auto feed_line = [&](std::string_view line, bool strip_leading)
      -> std::optional<std::string> {
    size_t i = 0;
    if (strip_leading) {
      while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    }
    for (; i < line.size(); ++i) {
      char c = line[i];
      if (escaped) {
        if (in_double_quote) {
          if (c != '$' && c != '`' && c != '"' && c != '\\') item += '\\';
          item += c;
        } else {
          item += c;
        }
        have_item = true;
        escaped = false;
        continue;
      }
      if (in_single_quote) {
        if (c == '\'') {
          in_single_quote = false;
        } else {
          item += c;
        }
        have_item = true;
        continue;
      }
      if (in_double_quote) {
        if (c == '"') {
          in_double_quote = false;
        } else if (c == '\\') {
          escaped = true;
        } else {
          item += c;
        }
        have_item = true;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        have_item = true;
      } else if (c == '\'') {
        in_single_quote = true;
        have_item = true;
      } else if (c == '"') {
        in_double_quote = true;
        have_item = true;
      } else if (c == '\r') {
        if (have_item) item += c;
      } else {
        item += c;
        have_item = true;
      }
    }
    return std::nullopt;
  };

  auto finish_item = [&]() {
    if (have_item) {
      args.push_back(item);
      item.clear();
      have_item = false;
    }
  };

  std::string line;
  while (std::getline(input, line)) {
    // The EOF marker comparison strips a trailing CR (CRLF input), but
    // the parsed item keeps it.
    std::string_view eof_candidate = line;
    if (!eof_candidate.empty() && eof_candidate.back() == '\r') {
      eof_candidate.remove_suffix(1);
    }
    if (eof_str && !eof_str->empty() && eof_candidate == *eof_str) {
      // A continued item is kept (its trailing backslash was dropped);
      // between items this is a no-op.
      finish_item();
      break;
    }

    if (!continuing && is_blank_input_line(line)) continue;

    if (pending_newline) {
      item += '\n';
      pending_newline = false;
    }
    if (auto error = feed_line(line, !continuing)) {
      return std::unexpected(*error);
    }
    if (in_single_quote || in_double_quote) {
      return std::unexpected(unmatched_error());
    }
    if (escaped) {
      // Trailing unquoted backslash: drop it and continue on the next
      // line.  The newline itself is only materialized if a next line
      // actually arrives (see pending_newline).
      escaped = false;
      continuing = true;
      pending_newline = true;
      continue;
    }
    continuing = false;
    finish_item();
  }

  if (in_single_quote || in_double_quote) {
    return std::unexpected(unmatched_error());
  }
  finish_item();
  return std::vector<std::string>(args.begin(), args.end());
}

auto parse_line_groups(std::istream &input, int max_lines,
                       const std::optional<std::string> &eof_str)
    -> cp::Result<std::vector<std::vector<std::string>>> {
  std::vector<std::vector<std::string>> groups;
  SmallVector<std::string, 256> current;
  int logical_lines = 0;
  bool continued_line = false;

  std::string line;
  while (std::getline(input, line)) {
    // #1321: -L no longer strips a trailing CR.  The EOF comparison still
    // ignores it, and a CR-only line still parses to zero parts (skipped
    // below) under the new CR rule in parse_default_arguments.
    std::string_view eof_candidate = line;
    if (!eof_candidate.empty() && eof_candidate.back() == '\r') {
      eof_candidate.remove_suffix(1);
    }
    if (eof_str && !eof_str->empty() && eof_candidate == *eof_str) break;

    bool ends_with_blank =
        !line.empty() && (line.back() == ' ' || line.back() == '\t');
    auto parsed_parts = split_blank_arguments(line);
    if (!parsed_parts) return std::unexpected(parsed_parts.error());
    auto parts = *parsed_parts;
    if (parts.empty() && !continued_line) continue;

    for (const auto &part : parts) current.push_back(part);

    if (ends_with_blank) {
      continued_line = true;
      continue;
    }

    if (!parts.empty() || continued_line) {
      ++logical_lines;
      continued_line = false;
    }

    if (logical_lines >= max_lines) {
      groups.emplace_back(current.begin(), current.end());
      current.clear();
      logical_lines = 0;
    }
  }

  if (continued_line && !current.empty()) ++logical_lines;
  if (!current.empty()) groups.emplace_back(current.begin(), current.end());

  return groups;
}

auto print_show_limits(int max_chars) -> void {
  // #1322: GNU prints six lines to stderr and then runs the command
  // normally; --show-limits suppresses nothing.  The Windows
  // ARG_MAX equivalent is the 32767 command-line limit; env is the real
  // environment block size in bytes, upper = 32767 - env - 2048, usable =
  // upper - env (both clamped at zero), buffer is the effective -s value,
  // smallest is 4096, and maximum parallelism is INT_MAX.
  size_t env_bytes = 0;
  LPWCH env = GetEnvironmentStringsW();
  if (env) {
    for (const wchar_t *p = env; *p != L'\0';) {
      size_t len = std::wcslen(p);
      env_bytes += (len + 1) * sizeof(wchar_t);
      p += len + 1;
    }
    FreeEnvironmentStringsW(env);
    env_bytes += sizeof(wchar_t);
  }
  long long upper =
      static_cast<long long>(kWindowsCommandLineLimit) -
      static_cast<long long>(env_bytes) - 2048;
  if (upper < 0) upper = 0;
  long long usable = upper - static_cast<long long>(env_bytes);
  if (usable < 0) usable = 0;
  int buffer_size = max_chars > 0 ? max_chars : kWindowsCommandLineLimit;

  safeErrorPrint("Your environment variables take up ");
  safeErrorPrint(std::to_string(env_bytes));
  safeErrorPrint(" bytes\n");
  safeErrorPrint("POSIX upper limit on argument length (this system): ");
  safeErrorPrint(std::to_string(upper));
  safeErrorPrint("\n");
  safeErrorPrint("POSIX smallest allowable upper limit on argument length (all "
            "systems): 4096\n");
  safeErrorPrint("Maximum length of command we could actually use: ");
  safeErrorPrint(std::to_string(usable));
  safeErrorPrint("\n");
  safeErrorPrint("Size of command buffer we are actually using: ");
  safeErrorPrint(std::to_string(buffer_size));
  safeErrorPrint("\n");
  safeErrorPrint("Maximum parallelism (--max-procs must be no greater): "
            "2147483647\n");
}

auto materialize_arguments(const std::vector<std::string> &base_args,
                           const std::vector<std::string> &input_args,
                           const std::string &replace_str)
    -> std::vector<std::string> {
  SmallVector<std::string, 256> all_args;

  for (const auto &base_arg : base_args) {
    if (!replace_str.empty() &&
        base_arg.find(replace_str) != std::string::npos) {
      std::string replaced = base_arg;
      std::string replacement;
      for (size_t j = 0; j < input_args.size(); ++j) {
        if (j > 0) replacement += " ";
        replacement += input_args[j];
      }

      size_t pos = 0;
      while ((pos = replaced.find(replace_str, pos)) != std::string::npos) {
        replaced.replace(pos, replace_str.length(), replacement);
        pos += replacement.length();
      }
      all_args.push_back(replaced);
    } else {
      all_args.push_back(base_arg);
    }
  }

  if (replace_str.empty()) {
    for (const auto &input_arg : input_args) {
      all_args.push_back(input_arg);
    }
  }

  return std::vector<std::string>(all_args.begin(), all_args.end());
}

auto expand_command_template_args(const SmallVector<std::string, 32> &base_args)
    -> std::vector<std::string> {
  SmallVector<std::string, 64> expanded_args;

  for (const auto &arg : base_args) {
    if (!contains_wildcard(arg)) {
      expanded_args.push_back(arg);
      continue;
    }

    auto glob_result = glob_expand(arg);
    if (glob_result.files.empty()) {
      expanded_args.push_back(arg);
      continue;
    }

    for (const auto &match : glob_result.files) {
      expanded_args.push_back(wstring_to_utf8(match));
    }
  }

  return std::vector<std::string>(expanded_args.begin(), expanded_args.end());
}

auto build_command_line(const std::string &command,
                        const std::vector<std::string> &args) -> std::wstring {
  std::wstring cmd_line = quote_windows_command_arg(utf8_to_wstring(command));
  auto is_cmd_shell = [](std::string_view value) {
    return ascii_iequals(value, "cmd") || ascii_iequals(value, "cmd.exe");
  };
  auto is_cmd_c = [](std::string_view value) {
    return value == "/C" || value == "/c";
  };
  auto cmd_escape_arg = [](std::string_view arg) {
    std::string escaped;
    escaped.reserve(arg.size() * 2);
    for (char ch : arg) {
      switch (ch) {
        case ' ':
        case '\t':
        case '^':
        case '&':
        case '|':
        case '<':
        case '>':
        case '(':
        case ')':
        case '"':
          escaped.push_back('^');
          break;
        default:
          break;
      }
      escaped.push_back(ch);
    }
    return escaped;
  };

  bool cmd_c_tail = false;
  for (const auto &arg : args) {
    cmd_line.push_back(L' ');
    if (cmd_c_tail) {
      cmd_line += utf8_to_wstring(cmd_escape_arg(arg));
    } else {
      cmd_line += quote_windows_command_arg(utf8_to_wstring(arg));
      if (is_cmd_shell(command) && is_cmd_c(arg)) {
        cmd_c_tail = true;
      }
    }
  }
  return cmd_line;
}

// #1322: the quoting used by -t and by the -p prompt (one function for
// both, as in GNU).  Quoting triggers on blanks, controls, and the
// punctuation set below; alphanumerics, `# % + , - . / : @ ] _ { } ~`
// and bytes >= 0x80 never trigger.  An argument holding a single quote
// but none of `" $ ` \ !` and no control character is double-quoted;
// anything else quoted goes single-quote style, with `'` as `'\''` and
// control characters as a closed quote + `$'\x'` + a reopened quote.
auto shell_argument_needs_quoting(unsigned char c) -> bool {
  if (c < 0x20 || c == 0x7f) return true;
  switch (c) {
    case ' ':
    case '!':
    case '"':
    case '$':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case ';':
    case '<':
    case '=':
    case '>':
    case '?':
    case '[':
    case '\\':
    case '^':
    case '`':
    case '|':
      return true;
    default:
      return false;
  }
}

auto quote_shell_argument(std::string_view arg) -> std::string {
  if (arg.empty()) return "''";

  bool needs_quoting = false;
  bool has_single_quote = false;
  bool double_quote_safe = true;
  for (unsigned char c : arg) {
    if (shell_argument_needs_quoting(c)) needs_quoting = true;
    if (c == '\'') has_single_quote = true;
    if (c == '"' || c == '$' || c == '`' || c == '\\' || c == '!' ||
        c < 0x20 || c == 0x7f) {
      double_quote_safe = false;
    }
  }
  if (!needs_quoting) return std::string(arg);

  if (has_single_quote && double_quote_safe) {
    return "\"" + std::string(arg) + "\"";
  }

  std::string out;
  out.push_back('\'');
  for (unsigned char c : arg) {
    if (c == '\'') {
      out += "'\\''";
    } else if (c < 0x20 || c == 0x7f) {
      out.push_back('\'');
      out += "$'";
      switch (c) {
        case '\a':
          out += "\\a";
          break;
        case '\b':
          out += "\\b";
          break;
        case '\t':
          out += "\\t";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\v':
          out += "\\v";
          break;
        case '\f':
          out += "\\f";
          break;
        case '\r':
          out += "\\r";
          break;
        default:
          out.push_back('\\');
          out.push_back(static_cast<char>('0' + ((c >> 6) & 0x7)));
          out.push_back(static_cast<char>('0' + ((c >> 3) & 0x7)));
          out.push_back(static_cast<char>('0' + (c & 0x7)));
          break;
      }
      out += "'";
      out.push_back('\'');
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  out.push_back('\'');
  return out;
}

auto quote_command_line(std::string_view command,
                        const std::vector<std::string> &args) -> std::string {
  std::string out = quote_shell_argument(command);
  for (const auto &arg : args) {
    out.push_back(' ');
    out += quote_shell_argument(arg);
  }
  return out;
}

enum class Confirmation { Yes, No, Fatal };

// #1320: GNU reads the -p answer from /dev/tty, never from the item
// stream, and fails loudly when the tty cannot be opened.  The Windows
// counterpart of the tty is CONIN$, but a Windows process can own a
// console that nobody attends: services, CI runners and test harnesses
// allocate or inherit one while every standard stream is a pipe, and a
// ReadFile on that console's input queue blocks forever.  CONIN$ is
// therefore used as the answer source only when this process's own
// output (stdout or stderr) is attached to a console -- positive
// evidence that a user is watching this terminal.  In every other
// posture the answer is read from stdin, the convention the cp/ln/rm
// and find -ok prompts already use in this tree: harnesses and agent
// drivers feed answers there, and with -a the item stream is a file so
// stdin is free to answer.  A read failure on either source is loud
// (Fatal), never a silent skip with exit code 0: when stdin was already
// consumed as the item source, the read fails at EOF and the run
// aborts instead of skipping every command.
auto output_attached_to_console() -> bool {
  auto is_console = [](DWORD which) -> bool {
    DWORD mode = 0;
    HANDLE h = GetStdHandle(which);
    return h != nullptr && h != INVALID_HANDLE_VALUE &&
           GetConsoleMode(h, &mode) != FALSE;
  };
  return is_console(STD_OUTPUT_HANDLE) || is_console(STD_ERROR_HANDLE);
}

auto read_confirmation() -> Confirmation {
  HANDLE console = INVALID_HANDLE_VALUE;
  if (output_attached_to_console()) {
    console = CreateFileW(L"CONIN$", GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, 0, nullptr);
  }
  if (console != nullptr && console != INVALID_HANDLE_VALUE) {
    std::string line;
    char buffer[256];
    DWORD read = 0;
    bool got_line = false;
    while (ReadFile(console, buffer, sizeof(buffer), &read, nullptr) &&
           read > 0) {
      for (DWORD i = 0; i < read; ++i) {
        if (buffer[i] == '\n') {
          got_line = true;
          break;
        }
        line.push_back(buffer[i]);
      }
      if (got_line) break;
    }
    CloseHandle(console);
    if (got_line) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      // Only the first character of the answer counts: a leading blank
      // is a refusal, not a yes.
      return !line.empty() && (line[0] == 'y' || line[0] == 'Y')
                 ? Confirmation::Yes
                 : Confirmation::No;
    }
    safeErrorPrint("xargs: Failed to read from stdin\n");
    return Confirmation::Fatal;
  }

  std::string response;
  if (!std::getline(std::cin, response)) {
    safeErrorPrint("xargs: Failed to read from stdin\n");
    return Confirmation::Fatal;
  }
  if (!response.empty() && response.back() == '\r') response.pop_back();
  return !response.empty() && (response[0] == 'y' || response[0] == 'Y')
             ? Confirmation::Yes
             : Confirmation::No;
}

auto confirm_command(std::string_view command,
                     const std::vector<std::string> &args) -> Confirmation {
  safeErrorPrint(quote_command_line(command, args));
  safeErrorPrint("?...");
  return read_confirmation();
}

struct ChildProcess {
  PROCESS_INFORMATION pi{};
  DWORD exit_code = 0;
  int slot = 0;
};

enum class ChildStdinMode { Parent, NullDevice, Console };

struct ScopedHandle {
  HANDLE handle = INVALID_HANDLE_VALUE;

  ScopedHandle() = default;
  explicit ScopedHandle(HANDLE h) : handle(h) {}
  ScopedHandle(const ScopedHandle &) = delete;
  auto operator=(const ScopedHandle &) -> ScopedHandle & = delete;
  ScopedHandle(ScopedHandle &&other) noexcept : handle(other.handle) {
    other.handle = INVALID_HANDLE_VALUE;
  }
  auto operator=(ScopedHandle &&other) noexcept -> ScopedHandle & {
    if (this != &other) {
      reset();
      handle = other.handle;
      other.handle = INVALID_HANDLE_VALUE;
    }
    return *this;
  }
  ~ScopedHandle() { reset(); }

  auto valid() const -> bool {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
  }

  auto reset(HANDLE h = INVALID_HANDLE_VALUE) -> void {
    if (valid()) CloseHandle(handle);
    handle = h;
  }
};

auto make_inheritable_file_handle(const wchar_t *path) -> ScopedHandle {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  return ScopedHandle(h);
}

auto append_env_entry(std::vector<wchar_t> &block, std::wstring_view entry)
    -> void {
  block.insert(block.end(), entry.begin(), entry.end());
  block.push_back(L'\0');
}

auto xargs_command_status_from_create_error(DWORD error) -> int {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return 127;
    default:
      return 126;
  }
}

auto xargs_windows_error_text(DWORD error) -> std::string {
  Win32ErrorTextOptions options;
  options.bad_exe_format_as_permission = true;
  return win32_posix_error_text(error, options);
}

auto report_xargs_input_file_open_error(std::string_view path) -> void {
  safeErrorPrint("xargs: Cannot open input file '");
  safeErrorPrint(path);
  safeErrorPrint("': ");

  std::error_code ec;
  auto status = std::filesystem::status(
      utf8_to_wstring(native_path::normalize_api_operand(path)), ec);
  if (ec) {
    safeErrorPrint(xargs_windows_error_text(static_cast<DWORD>(ec.value())));
  } else if (status.type() == std::filesystem::file_type::not_found) {
    safeErrorPrint("No such file or directory");
  } else if (status.type() == std::filesystem::file_type::directory) {
    safeErrorPrint("Is a directory");
  } else {
    safeErrorPrint("Permission denied");
  }
  safeErrorPrint("\n");
}

auto build_environment_block(std::string_view slot_var, int slot)
    -> std::vector<wchar_t> {
  if (slot_var.empty()) return {};

  std::wstring name = utf8_to_wstring(std::string(slot_var));
  std::wstring prefix = name + L"=";
  std::vector<wchar_t> block;

  LPWCH env = GetEnvironmentStringsW();
  if (env) {
    for (const wchar_t *p = env; *p != L'\0';) {
      std::wstring_view entry(p);
      bool same_name =
          entry.size() >= prefix.size() &&
          _wcsnicmp(entry.data(), prefix.c_str(), prefix.size()) == 0;
      if (!same_name) append_env_entry(block, entry);
      p += entry.size() + 1;
    }
    FreeEnvironmentStringsW(env);
  }

  append_env_entry(block, prefix + std::to_wstring(slot));
  block.push_back(L'\0');
  return block;
}

auto launch_process(const std::string &command,
                    const std::vector<std::string> &args,
                    ChildStdinMode stdin_mode,
                    std::string_view process_slot_var, int slot)
    -> cp::Result<ChildProcess> {
  // Resolve the child with GNU execvp semantics (PATH-only; no
  // application-directory, cwd, or System32 fallback) so a name colliding
  // with a Windows image resolves from the caller's PATH. Resolve failures
  // reuse the CreateProcess error channel so the caller keeps one
  // diagnostic/exit-status mapping.
  winux::childproc::ChildLaunchRequest xargs_launch_request;
  xargs_launch_request.command = command;
  xargs_launch_request.args = args;
  // xargs has its own command-aware quoting (caret escaping for cmd /C
  // tails); keep that line and let the helper only resolve the image and
  // route batch scripts through the command interpreter.
  xargs_launch_request.command_line_override =
      build_command_line(command, args);
  auto launch = winux::childproc::plan_child_launch(xargs_launch_request);
  if (launch.status != winux::childproc::ChildResolveStatus::Found) {
    return std::unexpected<cp::Error>(
        "create process failed:" +
        std::to_string(
            winux::childproc::child_status_to_win32_error(launch.status)));
  }
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi{};
  ScopedHandle stdin_handle;

  if (stdin_mode == ChildStdinMode::NullDevice) {
    stdin_handle = make_inheritable_file_handle(L"NUL");
    if (!stdin_handle.valid()) {
      return std::unexpected("failed to open NUL for child stdin");
    }
  } else if (stdin_mode == ChildStdinMode::Console) {
    stdin_handle = make_inheritable_file_handle(L"CONIN$");
    if (!stdin_handle.valid()) {
      return std::unexpected("failed to open console input for child stdin");
    }
  }

  if (stdin_handle.valid()) {
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = stdin_handle.handle;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  }

  auto env_block = build_environment_block(process_slot_var, slot);
  LPVOID environment = env_block.empty() ? nullptr : env_block.data();

  BOOL success = CreateProcessW(launch.application_name->c_str(),
                                launch.command_line.data(), nullptr, nullptr,
                                TRUE, CREATE_UNICODE_ENVIRONMENT, environment,
                                nullptr, &si, &pi);
  if (!success) {
    return std::unexpected<cp::Error>("create process failed:" +
                                      std::to_string(GetLastError()));
  }

  ChildProcess child;
  child.pi = pi;
  child.slot = slot;
  return child;
}

auto wait_child(ChildProcess &child) -> int {
  WaitForSingleObject(child.pi.hProcess, INFINITE);
  DWORD result = 0;
  GetExitCodeProcess(child.pi.hProcess, &result);
  CloseHandle(child.pi.hProcess);
  CloseHandle(child.pi.hThread);
  child.exit_code = result;
  return static_cast<int>(result);
}

auto wait_any_child(std::vector<ChildProcess> &running) -> size_t {
  if (running.empty()) return 0;
  if (running.size() == 1) return 0;
  if (running.size() > MAXIMUM_WAIT_OBJECTS) return 0;

  std::vector<HANDLE> handles;
  handles.reserve(running.size());
  for (const auto &child : running) {
    handles.push_back(child.pi.hProcess);
  }

  DWORD wait_result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                             handles.data(), FALSE, INFINITE);
  if (wait_result >= WAIT_OBJECT_0 &&
      wait_result < WAIT_OBJECT_0 + handles.size()) {
    return static_cast<size_t>(wait_result - WAIT_OBJECT_0);
  }

  return 0;
}

auto map_child_exit_status(int child_status) -> int {
  // #1319 (GNU): 1-254 -> 123; only 255 aborts with 124; a negative value
  // here means the DWORD exit code had its high bit set (an NTSTATUS-shaped
  // abnormal termination), which maps to 125 like a signal death.
  if (child_status == 0) return 0;
  if (child_status == 255) return 124;
  if (child_status >= 1 && child_status <= 254) return 123;
  return 125;
}

/**
 * @brief Execute command with arguments
 * @param command Command to execute
 * @param base_args Base arguments from command line
 * @param input_args Arguments from stdin
 * @param replace_str Replacement string
 * @param max_args Maximum arguments per execution
 * @param verbose Print command before execution
 * @return Exit code
 */
auto execute_command(const std::string &command,
                     const std::vector<std::string> &base_args,
                     const std::vector<std::string> &input_args,
                     const std::string &replace_str, int max_args,
                     int max_chars, bool exit_if_exceeded, int max_procs,
                     bool verbose, bool interactive, ChildStdinMode stdin_mode,
                     const std::string &process_slot_var) -> int {
  // #1323: -x needs no separate branch anymore: GNU errors on a single
  // item that fits nowhere with or without -x, and splits multi-item
  // batches with or without -x.
  (void)exit_if_exceeded;
  int exit_code = 0;
  bool stop_launching = false;

  if (max_args <= 0) {
    max_args = static_cast<int>(input_args.size());
  }
  if (!replace_str.empty()) max_args = 1;
  if (max_args <= 0) max_args = 1;
  if (max_procs <= 0) max_procs = 1;

  std::vector<ChildProcess> running;
  running.reserve(static_cast<size_t>(std::min(max_procs, 256)));
  SmallVector<int, 64> free_slots;
  int next_slot = 0;

  auto allocate_slot = [&]() -> int {
    if (!free_slots.empty()) {
      int slot = free_slots.back();
      free_slots.pop_back();
      return slot;
    }
    return next_slot++;
  };

  auto wait_one = [&]() {
    if (running.empty()) return;
    size_t completed_index = wait_any_child(running);
    int child_status = wait_child(running[completed_index]);
    if (child_status != 0) {
      exit_code = map_child_exit_status(child_status);
      if (child_status == 255) {
        // #1319: GNU prints the abort diagnostic for a 255 child.
        safeErrorPrint("xargs: ");
        safeErrorPrint(command);
        safeErrorPrint(": exited with status 255; aborting\n");
        stop_launching = true;
      }
    }
    if (!process_slot_var.empty()) {
      free_slots.push_back(running[completed_index].slot);
    }
    running.erase(running.begin() +
                  static_cast<std::ptrdiff_t>(completed_index));
  };

  SmallVector<std::string, 256> batch;
  batch.reserve(static_cast<size_t>(std::max(max_args, 1)));

  // #1323: -s accounting counts every argv entry (command + initial
  // arguments + this batch's items) as len+1 bytes of the original UTF-8
  // text, never the Windows-quoted command line (quoting inflates it and
  // used to shift every boundary by one).
  auto argv_size = [&](const std::vector<std::string> &materialized) {
    size_t total = command.size() + 1;
    for (const auto &arg : materialized) total += arg.size() + 1;
    return total;
  };
  auto base_argv_size = [&]() {
    return argv_size(materialize_arguments(base_args, {}, replace_str));
  };
  auto fits_limits = [&](const std::vector<std::string> &candidate) -> bool {
    if (max_args > 0 && static_cast<int>(candidate.size()) > max_args) {
      return false;
    }
    if (max_chars > 0) {
      auto materialized =
          materialize_arguments(base_args, candidate, replace_str);
      if (argv_size(materialized) > static_cast<size_t>(max_chars)) {
        return false;
      }
    }
    return true;
  };
  // #1323: GNU's two single-item errors: the command + initial arguments
  // alone already over the limit, versus a fitting base pushed over by
  // one item.
  auto report_oversized_single = [&]() {
    if (max_chars > 0 && base_argv_size() > static_cast<size_t>(max_chars)) {
      safeErrorPrint("xargs: cannot fit single argument within argument "
                     "list size limit\n");
    } else {
      safeErrorPrint("xargs: argument line too long\n");
    }
  };

  auto run_batch = [&](const std::vector<std::string> &current_batch,
                       bool allow_empty = false) -> bool {
    if (stop_launching) return false;
    if (current_batch.empty() && !allow_empty) return true;

    auto all_args =
        materialize_arguments(base_args, current_batch, replace_str);
    if (interactive) {
      auto decision = confirm_command(command, all_args);
      if (decision == Confirmation::Fatal) {
        exit_code = 1;
        stop_launching = true;
        return false;
      }
      if (decision == Confirmation::No) return true;
    }
    if (verbose) {
      safeErrorPrint(quote_command_line(command, all_args));
      safeErrorPrint("\n");
    }

    int slot = process_slot_var.empty() ? 0 : allocate_slot();
    auto child =
        launch_process(command, all_args, stdin_mode, process_slot_var, slot);
    if (!child) {
      DWORD error = 0;
      std::string_view raw_error = child.error();
      constexpr std::string_view kCreateProcessFailedPrefix =
          "create process failed:";
      if (raw_error.starts_with(kCreateProcessFailedPrefix)) {
        auto code_text = raw_error.substr(kCreateProcessFailedPrefix.size());
        auto parsed = std::from_chars(
            code_text.data(), code_text.data() + code_text.size(), error);
        if (parsed.ec != std::errc()) {
          error = 0;
        }
      }
      safeErrorPrint("xargs: ");
      safeErrorPrint(command);
      safeErrorPrint(": ");
      safeErrorPrint(xargs_windows_error_text(error));
      safeErrorPrint("\n");
      if (!process_slot_var.empty()) free_slots.push_back(slot);
      exit_code = xargs_command_status_from_create_error(error);
      stop_launching = true;
      return false;
    }

    running.push_back(*child);
    if (running.size() >= static_cast<size_t>(max_procs)) {
      wait_one();
      if (stop_launching) return false;
    }
    return true;
  };

  if (input_args.empty()) {
    // #1323: the command + initial arguments alone over the limit is an
    // error even with no items (and even without -x).
    if (!fits_limits({})) {
      report_oversized_single();
      return 1;
    }
    run_batch({}, true);
    while (!running.empty()) wait_one();
    return exit_code;
  }

  auto flush_batch = [&]() -> bool {
    if (batch.empty()) return true;
    bool ok = run_batch(std::vector<std::string>(batch.begin(), batch.end()));
    batch.clear();
    return ok;
  };

  auto drain_running_children = [&]() {
    while (!running.empty()) wait_one();
  };

  for (const auto &input_arg : input_args) {
    if (stop_launching) break;
    batch.push_back(input_arg);
    if (fits_limits(std::vector<std::string>(batch.begin(), batch.end()))) {
      continue;
    }

    batch.pop_back();
    if (!flush_batch()) {
      drain_running_children();
      return exit_code == 0 ? 1 : exit_code;
    }

    batch.push_back(input_arg);
    auto current_batch = std::vector<std::string>(batch.begin(), batch.end());
    if (!fits_limits(current_batch)) {
      // #1323: a single item that fits nowhere is an error even without
      // -x; GNU does not run it.
      drain_running_children();
      report_oversized_single();
      return 1;
    }
  }

  if (!flush_batch()) {
    drain_running_children();
    return exit_code == 0 ? 1 : exit_code;
  }

  drain_running_children();
  return exit_code;
}

}  // namespace xargs_pipeline

REGISTER_COMMAND(
    xargs, "xargs", "build and execute command lines from standard input",
    "Build and execute command lines from standard input.\n"
    "\n"
    "Items are separated by blanks. The result command line is executed\n"
    "after each group of max-args items is read.",
    "  find . -name '*.cpp' | xargs rm -f     Delete all cpp files\n"
    "  echo file1 file2 | xargs cat         Concatenate files\n"
    "  find . -name '*.txt' | xargs -n1 grep 'pattern'  Search one file at a "
    "time",
    "find(1), grep(1), sed(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    XARGS_OPTIONS) {
  using namespace xargs_pipeline;

  auto delimiter_mode = resolve_delimiter_mode(ctx);
  bool verbose =
      ctx.get<bool>("-t", false) || ctx.get<bool>("--verbose", false);
  bool interactive =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--interactive", false);
  bool no_run_if_empty =
      ctx.get<bool>("-r", false) || ctx.get<bool>("--no-run-if-empty", false);
  auto batch_options = normalize_batch_options(ctx);
  int max_args = batch_options.max_args;
  int max_lines = batch_options.max_lines;
  std::string replace_str = batch_options.replace_str;
  int max_procs = last_option_value<int>(ctx, "-P", "--max-procs", 1);
  int max_chars = last_option_value<int>(ctx, "-s", "--max-chars", 0);
  // Windows CreateProcess has a finite command-line limit.  GNU xargs
  // batches by default, so an omitted -s must still enforce that limit.
  if (max_chars == 0) max_chars = kWindowsCommandLineLimit;
  bool exit_if_exceeded =
      ctx.get<bool>("--exit", false) || ctx.get<bool>("-x", false);
  bool show_limits = ctx.get<bool>("--show-limits", false);
  if (max_args < 0 || (batch_options.max_args_present && max_args == 0)) {
    cp::report_custom_error(L"xargs", L"max-args must be positive");
    return 1;
  }
  if (max_procs < 0) {
    cp::report_custom_error(L"xargs", L"max-procs must be non-negative");
    return 1;
  }
  if (max_procs == 0) max_procs = std::numeric_limits<int>::max();
  if (max_chars < 0) {
    cp::report_custom_error(L"xargs", L"max-chars must be non-negative");
    return 1;
  }
  if (max_chars > kWindowsCommandLineLimit) {
    safeErrorPrint("xargs: warning: value for -s option is too large; using ");
    safeErrorPrint(std::to_string(kWindowsCommandLineLimit));
    safeErrorPrint("\n");
    max_chars = kWindowsCommandLineLimit;
  }
  if (max_lines < 0 || (batch_options.max_lines_present && max_lines == 0)) {
    cp::report_custom_error(L"xargs", L"max-lines must be positive");
    return 1;
  }
  // Delimiter state comes from resolve_delimiter_mode (last of -0/-d
  // wins); see that helper.
  std::string delimiter_arg = delimiter_mode.delimiter_arg;
  std::string arg_file = ctx.get<std::string>("--arg-file", "");
  if (arg_file.empty()) arg_file = ctx.get<std::string>("-a", "");
  std::string eof_arg = ctx.get<std::string>("-E", "");
  bool eof_enabled = ctx.has("-E");
  if (!eof_enabled && ctx.has("--eof")) {
    eof_arg = ctx.get<std::string>("--eof", "");
    eof_enabled = !eof_arg.empty();
  }
  if (!eof_enabled && ctx.has("-e")) {
    eof_arg = ctx.get<std::string>("-e", "");
    eof_enabled = !eof_arg.empty();
  }
  // #1322: -E has no effect under -0/-d; GNU warns once and continues.
  if (eof_enabled &&
      (delimiter_mode.use_null || delimiter_mode.has_delimiter)) {
    safeErrorPrint("xargs: warning: the -E option has no effect if -0 or "
                   "-d is used.\n");
  }
  std::string process_slot_var = ctx.get<std::string>("--process-slot-var", "");
  bool open_tty =
      ctx.get<bool>("-o", false) || ctx.get<bool>("--open-tty", false);

  if (option_present(ctx, "--process-slot-var") && process_slot_var.empty()) {
    cp::report_custom_error(L"xargs", L"process-slot-var must not be empty");
    return 1;
  }
  if (process_slot_var.find('=') != std::string::npos) {
    cp::report_custom_error(L"xargs", L"process-slot-var must not contain '='");
    return 1;
  }

  // [GNU] xargs.c:773 forces -x/--exit only for -I/-i (replace_pat) and
  // -L/-l (lines_per_exec); -d/--delimiter does not imply -x.
  if (!replace_str.empty() || max_lines > 0) {
    exit_if_exceeded = true;
  }

  char delimiter = delimiter_mode.use_null ? '\0' : ' ';
  bool split_blanks = !delimiter_mode.use_null && replace_str.empty();
  if (delimiter_mode.has_delimiter) {
    auto parsed_delim = parse_delimiter(delimiter_arg);
    if (!parsed_delim) {
      cp::report_error(parsed_delim, L"xargs");
      return 1;
    }
    delimiter = *parsed_delim;
    split_blanks = false;
  } else if (!replace_str.empty() && !delimiter_mode.use_null) {
    delimiter = '\n';
    split_blanks = false;
  }

  std::ifstream arg_input;
  std::istream *input = &std::cin;
  if (!arg_file.empty()) {
    arg_input.open(native_path::normalize_api_operand(arg_file),
                   std::ios::binary);
    if (!arg_input.is_open()) {
      report_xargs_input_file_open_error(arg_file);
      return 1;
    }
    input = &arg_input;
  }

  std::optional<std::string> logical_eof;
  if (!delimiter_mode.use_null && !delimiter_mode.has_delimiter &&
      eof_enabled) {
    logical_eof = eof_arg;
  }

  std::string raw_input_text((std::istreambuf_iterator<char>(*input)),
                             std::istreambuf_iterator<char>());
  std::istringstream parsed_input(raw_input_text);

  std::vector<std::vector<std::string>> input_groups;
  bool use_line_groups = max_lines > 0 && !delimiter_mode.use_null &&
                         !delimiter_mode.has_delimiter &&
                         replace_str.empty();
  if (use_line_groups) {
    auto parsed_groups =
        parse_line_groups(parsed_input, max_lines, logical_eof);
    if (!parsed_groups) {
      cp::report_error(parsed_groups, L"xargs");
      return 1;
    }
    input_groups = *parsed_groups;
  } else if (!replace_str.empty() && !delimiter_mode.use_null &&
             !delimiter_mode.has_delimiter) {
    auto input_args_vec =
        parse_replacement_arguments(parsed_input, logical_eof);
    if (!input_args_vec) {
      cp::report_error(input_args_vec, L"xargs");
      return 1;
    }
    input_groups.emplace_back(input_args_vec->begin(), input_args_vec->end());
  } else {
    auto input_args_vec =
        parse_arguments(parsed_input, delimiter, split_blanks, logical_eof);
    if (!input_args_vec) {
      cp::report_error(input_args_vec, L"xargs");
      return 1;
    }
    input_groups.emplace_back(input_args_vec->begin(), input_args_vec->end());
  }

  bool input_empty = true;
  for (const auto &group : input_groups) {
    if (!group.empty()) {
      input_empty = false;
      break;
    }
  }

  // Get command to execute (first positional arg)
  if (ctx.positionals.empty()) {
    // #1322: --show-limits prints the limits and execution continues
    // normally (GNU runs the command afterwards); it suppresses nothing.
    if (show_limits) print_show_limits(max_chars);
    // #1323: the echo command itself (5 bytes) over -s is an error even
    // with -r or --show-limits -- except -I with no items, where GNU
    // runs nothing at all.
    if (max_chars > 0 && static_cast<size_t>(max_chars) < 5 &&
        !( !replace_str.empty() && input_empty )) {
      safeErrorPrint("xargs: cannot fit single argument within argument "
                     "list size limit\n");
      return 1;
    }

    // Default to echo if no command specified
    // [GNU] xargs.c decides whether to run by checking whether any arguments
    // were accumulated, not by whether the raw input stream was byte-empty;
    // whitespace-only input yields zero arguments and therefore runs nothing.
    if (no_run_if_empty && input_empty) {
      return 0;
    }

    if (input_groups.empty()) input_groups.emplace_back();

    // #1323: same argv accounting as execute_command ("echo" counts 5).
    auto echo_fits_limits = [&](const std::vector<std::string> &echo_args) {
      if (max_args > 0 && static_cast<int>(echo_args.size()) > max_args) {
        return false;
      }
      if (max_chars > 0) {
        size_t total = 5;
        for (const auto &arg : echo_args) total += arg.size() + 1;
        if (total > static_cast<size_t>(max_chars)) {
          return false;
        }
      }
      return true;
    };
    auto echo_report_oversized_single = [&]() {
      if (max_chars > 0 && static_cast<size_t>(max_chars) < 5) {
        safeErrorPrint("xargs: cannot fit single argument within argument "
                       "list size limit\n");
      } else {
        safeErrorPrint("xargs: argument line too long\n");
      }
    };

    for (const auto &input_args : input_groups) {
      SmallVector<std::string, 256> batch;
      batch.reserve(input_args.size());
      const int echo_max_args = !replace_str.empty() ? 1 : max_args;

      auto flush_echo_batch = [&](bool allow_empty = false) -> bool {
        if (batch.empty() && !allow_empty) return true;

        std::vector<std::string> echo_args(batch.begin(), batch.end());
        // #1323: a batch that fits nowhere is an error even without -x.
        if (!echo_fits_limits(echo_args)) {
          echo_report_oversized_single();
          return false;
        }

        if (verbose) {
          safeErrorPrint(quote_command_line("echo", echo_args));
          safeErrorPrint("\n");
        }

        if (interactive) {
          auto decision = confirm_command("echo", echo_args);
          if (decision == Confirmation::Fatal) return false;
          if (decision == Confirmation::No) {
            batch.clear();
            return true;
          }
        }

        for (size_t i = 0; i < echo_args.size(); ++i) {
          if (i > 0) safePrint(" ");
          safePrint(echo_args[i]);
        }
        safePrint("\n");
        batch.clear();
        return true;
      };

      if (input_args.empty()) {
        // -I with no items runs nothing (GNU), even when the echo
        // command itself would exceed -s (same exception as the
        // explicit-command pre-check above).
        if (!replace_str.empty()) continue;
        if (!flush_echo_batch(true)) return 1;
        continue;
      }

      for (const auto &input_arg : input_args) {
        batch.push_back(input_arg);
        std::vector<std::string> candidate(batch.begin(), batch.end());
        if (echo_max_args > 0 &&
            static_cast<int>(candidate.size()) > echo_max_args) {
          batch.pop_back();
          if (!flush_echo_batch()) return 1;

          batch.push_back(input_arg);
          candidate.assign(batch.begin(), batch.end());
        }
        if (echo_fits_limits(candidate)) {
          continue;
        }

        batch.pop_back();
        if (!flush_echo_batch()) return 1;

        batch.push_back(input_arg);
        candidate.assign(batch.begin(), batch.end());
        if (!echo_fits_limits(candidate)) {
          // #1323: a single item that fits nowhere is an error even
          // without -x; GNU does not print it.
          echo_report_oversized_single();
          return 1;
        }
      }

      if (!flush_echo_batch()) {
        return 1;
      }
    }
    return 0;
  }

  std::string command = std::string(ctx.positionals[0]);
  SmallVector<std::string, 32> base_args;

  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    base_args.push_back(std::string(ctx.positionals[i]));
  }

  // #1322: --show-limits prints the limits and execution continues
  // normally (GNU runs the command afterwards); it suppresses nothing.
  if (show_limits) print_show_limits(max_chars);

  // Execute command with arguments - convert SmallVector to std::vector for
  // compatibility
  std::vector<std::string> base_args_vec =
      expand_command_template_args(base_args);
  // #1323: the old clamp lifted -s up to the command line and kept
  // running; GNU instead refuses when the command + initial arguments
  // alone exceed -s -- even with -r or --show-limits, and even with no
  // items.  The one exception is -I with no items, where GNU runs
  // nothing at all.  (With the default -s of 32767 this never fires.)
  if (max_chars > 0 && !( !replace_str.empty() && input_empty )) {
    size_t base_size = command.size() + 1;
    for (const auto &arg : base_args_vec) base_size += arg.size() + 1;
    if (base_size > static_cast<size_t>(max_chars)) {
      safeErrorPrint("xargs: cannot fit single argument within argument "
                     "list size limit\n");
      return 1;
    }
  }

  // If no input arguments, check if we should skip execution
  if (input_empty) {
    // Skip if -r (no-run-if-empty) is specified
    // [GNU] xargs.c gates -r/--no-run-if-empty on the number of accumulated
    // arguments (none), not on raw input being byte-empty.
    if (no_run_if_empty && input_empty) {
      return 0;
    }
    // Skip if -I is specified but there's nothing to replace
    if (!replace_str.empty()) {
      return 0;
    }
    if (input_groups.empty()) input_groups.emplace_back();
  }

  ChildStdinMode stdin_mode = ChildStdinMode::NullDevice;
  if (open_tty) {
    stdin_mode = ChildStdinMode::Console;
  } else if (!arg_file.empty()) {
    stdin_mode = ChildStdinMode::Parent;
  }
  int exit_code = 0;
  for (const auto &input_args : input_groups) {
    int status =
        execute_command(command, base_args_vec, input_args, replace_str,
                        max_args, max_chars, exit_if_exceeded, max_procs,
                        verbose, interactive, stdin_mode, process_slot_var);
    if (status != 0) exit_code = status;
    if (status == 124 || status == 126 || status == 127) return status;
  }
  return exit_code;
}
