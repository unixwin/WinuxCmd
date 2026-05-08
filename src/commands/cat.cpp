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
 *  - File: cat.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for cat.
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
 * @brief CAT command options definition
 *
 * This array defines all the options supported by the cat command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -A, @a --show-all: Equivalent to -vET [IMPLEMENTED]
 * - @a -b, @a --number-nonblank: Number nonempty output lines, overrides -n
 * [IMPLEMENTED]
 * - @a -e: Equivalent to -vE [IMPLEMENTED]
 * - @a -E, @a --show-ends: Display $ at end of each line [IMPLEMENTED]
 * - @a -n, @a --number: Number all output lines [IMPLEMENTED]
 * - @a -s, @a --squeeze-blank: Suppress repeated empty output lines
 * [IMPLEMENTED]
 * - @a -t: Equivalent to -vT [IMPLEMENTED]
 * - @a -T, @a --show-tabs: Display TAB characters as ^I [IMPLEMENTED]
 * - @a -u: (ignored, for POSIX compatibility) [IMPLEMENTED]
 * - @a -v, @a --show-nonprinting: Use ^ and M- notation, except for LFD and TAB
 * [IMPLEMENTED]
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// clang-format off
auto constexpr CAT_OPTIONS =
    std::array{OPTION("-A", "--show-all", "equivalent to -vET"),
               OPTION("-b", "--number-nonblank", "number nonempty output lines, overrides -n"),
               OPTION("-e", "", "equivalent to -vE"),
               OPTION("-E", "--show-ends", "display $ at end of each line"),
               OPTION("-n", "--number", "number all output lines"),
               OPTION("-s", "--squeeze-blank", "suppress repeated empty output lines"),
               OPTION("-t", "", "equivalent to -vT"),
               OPTION("-T", "--show-tabs", "display TAB characters as ^I"),
               OPTION("-u", "", "(ignored, for POSIX compatibility)"),
               OPTION("-v", "--show-nonprinting", "use ^ and M- notation, except for LFD and TAB")};
// clang-format on

namespace cat_pipeline {
namespace cp = core::pipeline;

// ----------------------------------------------
// 1. Validate arguments - OPTIMIZED: pass by reference
// ----------------------------------------------
auto validate_arguments(const CommandContext<CAT_OPTIONS.size()> &ctx,
                        SmallVector<std::string, 64> &out_files)
    -> cp::Result<void> {
  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);

    // Smart glob expansion for wildcard patterns
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        // Pattern was expanded, add all matched files
        for (const auto &file : glob_result.files) {
          out_files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }

    // Not a wildcard or expansion failed, use as-is
    out_files.push_back(file_arg);
  }

  if (out_files.empty()) {
    out_files.push_back("-");
  }

  return {};
}

}  // namespace cat_pipeline

REGISTER_COMMAND(cat, "cat",
                 "concatenate files and print on the standard output",
                 "Concatenate FILE(s) to standard output.\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\nExamples:\n"
                 "  cat f g  Output f's contents, then g's contents.\n"
                 "  cat      Copy standard input to standard output.",
                 "  cat file.txt              Display contents of file.txt\n"
                 "  cat -n file.txt           Number all output lines\n"
                 "  cat file1.txt file2.txt   Concatenate multiple files\n"
                 "  cat                       Read from standard input",
                 "tac(1), head(1), tail(1), more(1), less(1)", "caomengxuan666",
                 "Copyright © 2026 WinuxCmd", CAT_OPTIONS) {
  using namespace cat_pipeline;
  using namespace core::pipeline;

  const bool fast_passthrough =
      !ctx.get<bool>("--show-all", false) &&
      !ctx.get<bool>("--number-nonblank", false) &&
      !ctx.get<bool>("-b", false) && !ctx.get<bool>("--show-ends", false) &&
      !ctx.get<bool>("-E", false) && !ctx.get<bool>("--number", false) &&
      !ctx.get<bool>("-n", false) && !ctx.get<bool>("--squeeze-blank", false) &&
      !ctx.get<bool>("-s", false) && !ctx.get<bool>("--show-tabs", false) &&
      !ctx.get<bool>("-T", false) &&
      !ctx.get<bool>("--show-nonprinting", false) &&
      !ctx.get<bool>("-v", false) && !ctx.get<bool>("-e", false) &&
      !ctx.get<bool>("-t", false);

  // ----------------------------------------------
  // Empty line check (unchanged)
  // ----------------------------------------------
  auto is_empty_line = [](const std::string &line) -> bool {
    return line.empty() ||
           (line.size() == 1 && isspace(static_cast<unsigned char>(line[0])));
  };

  // ----------------------------------------------
  // Process character - FULLY OPTIMIZED, NO wstring!
  // ----------------------------------------------
  auto process_character = [&](unsigned char c,
                               const CommandContext<CAT_OPTIONS.size()> &ctx) {
    bool show_nonprinting = ctx.get<bool>("--show-nonprinting", false) ||
                            ctx.get<bool>("--show-all", false) ||
                            ctx.get<bool>("-e", false) ||
                            ctx.get<bool>("-t", false);
    bool show_ends = ctx.get<bool>("--show-ends", false) ||
                     ctx.get<bool>("--show-all", false) ||
                     ctx.get<bool>("-e", false);
    bool show_tabs = ctx.get<bool>("--show-tabs", false) ||
                     ctx.get<bool>("--show-all", false) ||
                     ctx.get<bool>("-t", false);

    if (show_nonprinting) {
      if (c < 0x20) {
        // Control characters
        if (c == '\n') {
          safePrint("\n");
        } else if (c == '\t') {
          if (show_tabs)
            safePrint("^I");
          else
            safePrint("\t");
        } else if (c == '\f') {
          safePrint("^L");
        } else {
          // ^A, ^B, ..., ^Z - NO wstring!
          safePrint('^');
          safePrint(static_cast<char>(c + 0x40));
        }
      } else if (c == 0x7F) {
        // DEL
        safePrint("^?");
      } else if (c >= 0x80) {
        // M-x notation - NO wstring!
        safePrint('M');
        safePrint('-');
        safePrint(static_cast<char>(c - 0x80));
      } else {
        // Printable ASCII
        safePrint(static_cast<char>(c));
      }
    } else if (show_tabs && c == '\t') {
      safePrint("^I");
    } else {
      // Normal output
      safePrint(static_cast<char>(c));
    }
  };

  // ----------------------------------------------
  // Process line - OPTIMIZED: snprintf instead of wostringstream
  // ----------------------------------------------
  auto process_line = [&](std::string &line,
                          const CommandContext<CAT_OPTIONS.size()> &ctx,
                          size_t &line_num) {
    // Remove Windows line endings
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    bool empty = is_empty_line(line);

    bool number_lines = ctx.get<bool>("--number", false);
    bool number_nonblank = ctx.get<bool>("--number-nonblank", false);
    bool show_ends = ctx.get<bool>("--show-ends", false) ||
                     ctx.get<bool>("--show-all", false) ||
                     ctx.get<bool>("-e", false);

    // OPTIMIZED: snprintf instead of wostringstream
    if (number_lines && !number_nonblank) {
      char buf[32];
      int len = snprintf(buf, sizeof(buf), "%6zu ", line_num++);
      safePrint(std::string_view(buf, len));
    } else if (number_nonblank && !empty) {
      char buf[32];
      int len = snprintf(buf, sizeof(buf), "%6zu ", line_num++);
      safePrint(std::string_view(buf, len));
    }

    // Output content
    for (char ch : line) {
      process_character(static_cast<unsigned char>(ch), ctx);
    }

    if (show_ends) safePrint("$");
    safePrint("\n");
  };

  // ----------------------------------------------
  // Process stream (unchanged)
  // ----------------------------------------------
  auto process_stream = [&](std::istream &stream,
                            const CommandContext<CAT_OPTIONS.size()> &ctx,
                            size_t &line_num) {
    if (fast_passthrough) {
      std::array<char, 8192> buffer{};
      while (stream.good()) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto got = stream.gcount();
        if (got <= 0) break;
        safePrint(std::string_view(buffer.data(), static_cast<size_t>(got)));
        if (is_stdout_pipe_closed()) break;
      }
      return;
    }

    std::string line;
    bool last_line_empty = false;
    bool squeeze_blank = ctx.get<bool>("--squeeze-blank", false);

    while (std::getline(stream, line)) {
      bool empty = is_empty_line(line);

      if (squeeze_blank && empty && last_line_empty) {
        continue;
      }

      last_line_empty = empty;
      process_line(line, ctx, line_num);

      // Downstream (for example `head`) may close the pipe early.
      // Stop reading immediately instead of scanning the rest of huge inputs.
      if (is_stdout_pipe_closed()) {
        break;
      }
    }
  };

  // ----------------------------------------------
  // Process file - OPTIMIZED: NO wstring error messages!
  // ----------------------------------------------
  auto process_file = [&](std::string_view path,
                          const CommandContext<CAT_OPTIONS.size()> &ctx,
                          size_t &line_num) -> bool {
    if (path == "-") {
      process_stream(std::cin, ctx, line_num);
      return true;
    }

    std::ifstream file(std::string(path), std::ios::binary);

    if (!file.is_open()) {
      // OPTIMIZED: No wstring concatenation!
      safeErrorPrint("cat: '");
      safeErrorPrint(path);
      safeErrorPrint("': No such file or directory");
      safeErrorPrint("\n");
      return false;
    }

    process_stream(file, ctx, line_num);

    if (file.bad()) {
      safeErrorPrint("cat: error reading '");
      safeErrorPrint(path);
      safeErrorPrint("'");
      safeErrorPrint("\n");
      return false;
    }

    return true;
  };

  // ----------------------------------------------
  // Main - OPTIMIZED: pass vector by reference
  // ----------------------------------------------
  SmallVector<std::string, 64> files;
  auto result = validate_arguments(ctx, files);
  if (!result) return 1;

  int exit_code = 0;
  size_t line_num = 1;
  clear_pipe_closed_flags();

  for (const auto &file : files) {
    if (is_stdout_pipe_closed()) {
      break;
    }
    if (!process_file(file, ctx, line_num)) {
      exit_code = 1;
    }
  }

  return exit_code;
}
