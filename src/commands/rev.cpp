// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for rev command.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr REV_OPTIONS =
    // [EXT]
    std::array{OPTION("-0", "--zero", "use the NUL byte as line separator")};

namespace {

[[nodiscard]] auto utf8_unit_length(std::string_view text, size_t pos)
    -> size_t {
  unsigned char c = static_cast<unsigned char>(text[pos]);
  size_t len = 1;
  if ((c & 0x80U) == 0) {
    len = 1;
  } else if ((c & 0xE0U) == 0xC0U) {
    len = 2;
  } else if ((c & 0xF0U) == 0xE0U) {
    len = 3;
  } else if ((c & 0xF8U) == 0xF0U) {
    len = 4;
  }

  if (pos + len > text.size()) return 1;
  for (size_t i = 1; i < len; ++i) {
    unsigned char next = static_cast<unsigned char>(text[pos + i]);
    if ((next & 0xC0U) != 0x80U) return 1;
  }
  return len;
}

[[nodiscard]] auto reverse_record_body(std::string_view body) -> std::string {
  std::vector<std::string_view> units;
  units.reserve(body.size());
  for (size_t pos = 0; pos < body.size();) {
    size_t len = utf8_unit_length(body, pos);
    units.push_back(body.substr(pos, len));
    pos += len;
  }

  std::string out;
  out.reserve(body.size());
  for (auto it = units.rbegin(); it != units.rend(); ++it) {
    out.append(it->begin(), it->end());
  }
  return out;
}

auto write_reversed_record(std::string_view record, char separator) -> void {
  bool has_separator = !record.empty() && record.back() == separator;
  std::string_view body =
      has_separator ? record.substr(0, record.size() - 1) : record;
  std::string reversed = reverse_record_body(body);
  if (has_separator) reversed.push_back(separator);
  safePrint(std::string_view(reversed));
}

auto process_stream(std::istream& input, char separator) -> bool {
  std::string record;
  char ch = '\0';
  while (input.get(ch)) {
    record.push_back(ch);
    if (ch == separator) {
      write_reversed_record(record, separator);
      record.clear();
    }
  }
  if (!record.empty()) {
    write_reversed_record(record, separator);
  }
  return !input.bad();
}

auto expand_file_operands(std::span<const std::string_view> positionals)
    -> std::vector<std::string> {
  std::vector<std::string> files;
  for (const auto& arg : positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    files.push_back(std::move(file_arg));
  }
  return files;
}

}  // namespace

REGISTER_COMMAND(
    rev_cmd,
    /* name */
    "rev",

    /* synopsis */
    "rev [OPTION] [FILE]...",
    "Reverse lines characterwise.\n"
    "\n"
    "Print each specified record in reverse character order. Records are "
    "newline-terminated by default; -0, --zero uses NUL as the separator. If "
    "no "
    "FILE is specified, read from standard input. This follows util-linux "
    "rev's "
    "record model, including preserving unterminated final records.",
    "  echo 'hello' | rev\n"
    "  rev input.txt\n"
    "  rev -0 names.bin",

    /* see also */
    "tac(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", REV_OPTIONS) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif
  const char separator =
      (ctx.get<bool>("-0", false) || ctx.get<bool>("--zero", false)) ? '\0'
                                                                     : '\n';
  auto files = expand_file_operands(ctx.positionals);

  if (files.empty()) {
    if (!process_stream(std::cin, separator)) {
      safeErrorPrintLn("rev: error reading standard input");
      return 1;
    }
    return 0;
  }

  int exit_code = 0;
  for (const auto& file : files) {
    std::ifstream input(std::filesystem::path(utf8_to_wstring(
                            native_path::normalize_api_operand(file))),
                        std::ios::binary);
    if (!input) {
      safeErrorPrintLn("rev: cannot open '" + file +
                       "': No such file or directory");
      exit_code = 1;
      continue;
    }
    if (!process_stream(input, separator)) {
      safeErrorPrintLn("rev: error reading '" + file + "'");
      exit_code = 1;
    }
  }

  return exit_code;
}
