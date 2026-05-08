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
 *  - File: tr.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tr.
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

auto constexpr TR_OPTIONS = std::array{
    OPTION("-c", "--complement", "use the complement of SET1", BOOL_TYPE),
    OPTION("-C", "", "same as -c", BOOL_TYPE),
    OPTION("-d", "--delete", "delete characters in SET1, do not translate",
           BOOL_TYPE),
    OPTION("-s", "--squeeze-repeats",
           "replace each input sequence of a repeated character that is listed "
           "in SET1 with a single occurrence of that character",
           BOOL_TYPE),
    OPTION("-t", "--truncate-set1", "first truncate SET1 to length of SET2",
           BOOL_TYPE)};

namespace tr_pipeline {
namespace cp = core::pipeline;

// Parse escape sequences
auto parse_escape_sequence(std::string_view& str) -> char {
  if (str.empty()) return '\\';

  char c = str[0];
  str = str.substr(1);

  switch (c) {
    case 'n':
      return '\n';
    case 't':
      return '\t';
    case 'r':
      return '\r';
    case 'b':
      return '\b';
    case 'f':
      return '\f';
    case 'v':
      return '\v';
    case 'a':
      return '\a';
    case '\\':
      return '\\';
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
      // Parse octal \NNN
      int value = 0;
      int digits = 0;
      while (digits < 3 && !str.empty() && str[0] >= '0' && str[0] <= '7') {
        value = value * 8 + (str[0] - '0');
        str = str.substr(1);
        digits++;
      }
      return static_cast<char>(value);
    }
    default:
      return c;
  }
}

// Parse a character set string
auto parse_set(std::string_view str) -> std::string {
  std::string result;
  result.reserve(str.size());

  while (!str.empty()) {
    if (str[0] == '\\') {
      str = str.substr(1);
      if (!str.empty()) {
        result += parse_escape_sequence(str);
      }
    } else if (str.size() >= 3 && str[1] == '-') {
      // Range: A-Z
      char start = str[0];
      char end = str[2];
      for (char c = start; c <= end; ++c) {
        result += c;
      }
      str = str.substr(3);
    } else {
      result += str[0];
      str = str.substr(1);
    }
  }

  return result;
}

// Build translation table
void build_translation_table(const std::string& set1, const std::string& set2,
                             bool complement, bool truncate_set1,
                             std::array<char, 256>& table) {
  // Initialize table to no change
  for (int i = 0; i < 256; ++i) {
    table[i] = static_cast<char>(i);
  }

  std::string effective_set1 = set1;
  std::string effective_set2 = set2;

  // Handle complement
  if (complement) {
    std::string temp;
    for (int i = 0; i < 256; ++i) {
      char c = static_cast<char>(i);
      if (set1.find(c) == std::string::npos) {
        temp += c;
      }
    }
    effective_set1 = temp;
  }

  // Handle truncate
  if (truncate_set1 && effective_set1.size() > effective_set2.size()) {
    effective_set1 = effective_set1.substr(0, effective_set2.size());
  }

  // Build translation
  size_t len = std::min(effective_set1.size(), effective_set2.size());
  for (size_t i = 0; i < len; ++i) {
    table[static_cast<unsigned char>(effective_set1[i])] = effective_set2[i];
  }

  // If set2 is shorter, repeat last character
  if (!effective_set2.empty() &&
      effective_set1.size() > effective_set2.size()) {
    char last = effective_set2.back();
    for (size_t i = effective_set2.size(); i < effective_set1.size(); ++i) {
      table[static_cast<unsigned char>(effective_set1[i])] = last;
    }
  }
}

// Build delete set
auto build_delete_set(const std::string& set1, bool complement)
    -> std::array<bool, 256> {
  std::array<bool, 256> delete_set{};
  delete_set.fill(false);

  if (complement) {
    // Mark all characters as to be deleted
    delete_set.fill(true);
    // Unmark characters in set1
    for (char c : set1) {
      delete_set[static_cast<unsigned char>(c)] = false;
    }
  } else {
    // Mark characters in set1 as to be deleted
    for (char c : set1) {
      delete_set[static_cast<unsigned char>(c)] = true;
    }
  }

  return delete_set;
}

// Build squeeze set
auto build_squeeze_set(const std::string& set1, bool complement)
    -> std::array<bool, 256> {
  std::array<bool, 256> squeeze_set{};
  squeeze_set.fill(false);

  if (complement) {
    squeeze_set.fill(true);
    for (char c : set1) {
      squeeze_set[static_cast<unsigned char>(c)] = false;
    }
  } else {
    for (char c : set1) {
      squeeze_set[static_cast<unsigned char>(c)] = true;
    }
  }

  return squeeze_set;
}

struct Config {
  bool complement = false;
  bool delete_mode = false;
  bool squeeze = false;
  bool truncate_set1 = false;
  std::string set1;
  std::string set2;
};

auto build_config(const CommandContext<TR_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.complement = ctx.get<bool>("--complement", false) ||
                   ctx.get<bool>("-c", false) || ctx.get<bool>("-C", false);
  cfg.delete_mode =
      ctx.get<bool>("--delete", false) || ctx.get<bool>("-d", false);
  cfg.squeeze =
      ctx.get<bool>("--squeeze-repeats", false) || ctx.get<bool>("-s", false);
  cfg.truncate_set1 =
      ctx.get<bool>("--truncate-set1", false) || ctx.get<bool>("-t", false);

  // For -ds mode, both sets are needed
  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  cfg.set1 = parse_set(ctx.positionals[0]);

  // In -ds mode, the second argument is SET2 (set to squeeze), not SET2
  // (translation)
  if (ctx.positionals.size() > 1) {
    cfg.set2 = parse_set(ctx.positionals[1]);
  } else {
    cfg.set2 = "";  // Default empty set
  }

  if (ctx.positionals.size() > 2) {
    return std::unexpected("extra operand");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  std::array<char, 256> trans_table;
  std::array<bool, 256> delete_set{};
  std::array<bool, 256> squeeze_set{};

  // Build tables based on mode
  if (cfg.delete_mode) {
    delete_set = build_delete_set(cfg.set1, cfg.complement);
  } else {
    build_translation_table(cfg.set1, cfg.set2, cfg.complement,
                            cfg.truncate_set1, trans_table);
  }

  if (cfg.squeeze) {
    // When using -ds, use SET2 for squeeze; otherwise use SET1
    if (cfg.delete_mode && !cfg.set2.empty()) {
      squeeze_set = build_squeeze_set(cfg.set2, false);
    } else {
      squeeze_set = build_squeeze_set(cfg.set1, cfg.complement);
    }
  }

  // Check if we're in -ds mode (delete + squeeze with SET2)
  bool is_delete_squeeze_mode =
      cfg.delete_mode && cfg.squeeze && !cfg.set2.empty();

  // Read from stdin and process
  std::string input;
  input.assign(std::istreambuf_iterator<char>(std::cin),
               std::istreambuf_iterator<char>());

  std::string output;
  output.reserve(input.size());

  char prev_char = '\0';
  bool prev_was_squeezed = false;

  for (char c : input) {
    unsigned char uc = static_cast<unsigned char>(c);

    // Check delete (SET1 in delete mode)
    if (cfg.delete_mode && delete_set[uc]) {
      continue;
    }

    // Translate
    char translated = cfg.delete_mode ? c : trans_table[uc];

    // Squeeze
    bool should_output = true;
    if (cfg.squeeze && squeeze_set[uc]) {
      // This character is in the squeeze set
      // If it's a repeat of the previous character, skip it
      if (prev_was_squeezed && translated == prev_char) {
        should_output = false;
      }
      // In -ds mode, also delete single non-repeated characters in SET2
      if (is_delete_squeeze_mode && should_output && !prev_was_squeezed) {
        // Look ahead to see if this character repeats
        bool has_repeat = false;
        for (size_t i = &c - input.data() + 1; i < input.size(); ++i) {
          unsigned char next_uc = static_cast<unsigned char>(input[i]);
          if (!cfg.delete_mode || !delete_set[next_uc]) {
            char next_translated =
                cfg.delete_mode ? input[i] : trans_table[next_uc];
            if (squeeze_set[next_uc] && next_translated == translated) {
              has_repeat = true;
              break;
            }
          }
        }
        if (!has_repeat) {
          should_output = false;
        }
      }
    }

    if (should_output) {
      output += translated;
      prev_char = translated;
      prev_was_squeezed = (cfg.squeeze && squeeze_set[uc]);
    }
    // Note: If we skip a character (should_output = false), we keep
    // prev_was_squeezed = true to continue skipping subsequent same characters
  }

  safePrint(output);
  return 0;
}

}  // namespace tr_pipeline

REGISTER_COMMAND(
    tr, "tr", "tr [OPTION]... SET1 [SET2]",
    "Translate, squeeze, and/or delete characters from standard input,\n"
    "writing to standard output.\n"
    "\n"
    "SET1 and SET2 specify arrays of characters to translate, delete,\n"
    "or squeeze. Interpreted sequences are:\n"
    "  \\\\NNN  character with octal value NNN (1 to 3 digits)\n"
    "  \\\\\\\\\\  backslash\n"
    "  \\a    audible BEL\n"
    "  \\b    backspace\n"
    "  \\f    form feed\n"
    "  \\n    new line\n"
    "  \\r    return\n"
    "  \\t    horizontal tab\n"
    "  \\v    vertical tab\n"
    "CHAR1-CHAR2  all characters from CHAR1 to CHAR2 in ascending order",
    "  echo 'hello' | tr 'a-z' 'A-Z'\n"
    "  echo 'hello   world' | tr -s ' '\n"
    "  echo 'Hello World' | tr -d 'a-z'\n"
    "  echo 'Hello World' | tr -c 'A-Z' '*'",
    "expand(1), unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    TR_OPTIONS) {
  using namespace tr_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"tr");
    return 1;
  }

  return run(*cfg_result);
}
