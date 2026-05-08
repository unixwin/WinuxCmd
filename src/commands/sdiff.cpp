/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: sdiff.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sdiff command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr SDIFF_OPTIONS =
    std::array{OPTION("-o", "", "output file", STRING_TYPE),
               OPTION("-w", "", "ignore all whitespace"),
               OPTION("-B", "", "ignore changes whose lines are all blank"),
               OPTION("-E", "", "ignore tab expansion"),
               OPTION("-b", "", "ignore changes in amount of white space"),
               OPTION("-W", "", "ignore all white space")};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Trim whitespace from string
std::string trim_whitespace(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\n\r");
  return s.substr(start, end - start + 1);
}

// Remove all whitespace
std::string remove_whitespace(const std::string& s) {
  std::string result;
  for (char c : s) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      result += c;
    }
  }
  return result;
}

// Compare lines with options
bool lines_equal(const std::string& line1, const std::string& line2,
                 bool ignore_whitespace, bool ignore_all_whitespace) {
  if (ignore_all_whitespace) {
    return remove_whitespace(line1) == remove_whitespace(line2);
  } else if (ignore_whitespace) {
    return trim_whitespace(line1) == trim_whitespace(line2);
  } else {
    return line1 == line2;
  }
}

// Format line with padding
std::string format_line(const std::string& line, int width, char marker = ' ') {
  std::string result;
  result += marker;
  result += " ";

  if (line.length() < static_cast<size_t>(width)) {
    result += line;
    result.append(width - line.length(), ' ');
  } else {
    result += line.substr(0, width);
  }

  return result;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    sdiff,
    /* cmd_name */ "sdiff",
    /* cmd_synopsis */ "sdiff [OPTION] FILE1 FILE2",
    /* cmd_desc */
    "Side-by-side merge of differences between FILE1 and FILE2.\n"
    "Display two files side by side, with differences marked.\n"
    "Common lines are shown normally. Differences are marked with |,<,>.",
    /* examples */
    "  sdiff file1.txt file2.txt\n"
    "  sdiff -w file1.txt file2.txt\n"
    "  sdiff -o merged.txt file1.txt file2.txt",
    /* see_also */ "diff, diff3, patch",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ SDIFF_OPTIONS) {
  std::string output_file = ctx.get<std::string>("-o", "");
  bool ignore_whitespace =
      ctx.get<bool>("-w", false) || ctx.get<bool>("-b", false);
  bool ignore_all_whitespace = ctx.get<bool>("-W", false);
  bool ignore_blank = ctx.get<bool>("-B", false);
  bool ignore_tab_expansion = ctx.get<bool>("-E", false);

  if (ctx.positionals.size() < 2) {
    safeErrorPrintLn("sdiff: missing file operands");
    safePrintLn("Usage: sdiff [OPTION] FILE1 FILE2");
    return 1;
  }

  std::string file1 = std::string(ctx.positionals[0]);
  std::string file2 = std::string(ctx.positionals[1]);

  // Read files
  std::vector<std::string> lines1 = read_file_lines(file1);
  std::vector<std::string> lines2 = read_file_lines(file2);

  if (lines1.empty()) {
    safeErrorPrintLn("sdiff: cannot read file '" + file1 + "'");
    return 1;
  }
  if (lines2.empty()) {
    safeErrorPrintLn("sdiff: cannot read file '" + file2 + "'");
    return 1;
  }

  // Column width (adjustable based on terminal)
  int col_width = 40;

  // Compare lines side by side
  std::vector<std::string> output;

  size_t idx1 = 0;
  size_t idx2 = 0;

  while (idx1 < lines1.size() || idx2 < lines2.size()) {
    if (idx1 >= lines1.size()) {
      // Only file2 has remaining lines
      output.push_back(format_line("", col_width, '<') + "  " +
                       format_line(lines2[idx2++], col_width));
    } else if (idx2 >= lines2.size()) {
      // Only file1 has remaining lines
      output.push_back(format_line(lines1[idx1++], col_width, '>') + "  " +
                       format_line("", col_width));
    } else {
      const std::string& line1 = lines1[idx1];
      const std::string& line2 = lines2[idx2];

      // Check for blank lines
      bool line1_blank = line1.find_first_not_of(" \t") == std::string::npos;
      bool line2_blank = line2.find_first_not_of(" \t") == std::string::npos;

      if (ignore_blank && line1_blank && line2_blank) {
        // Both blank, skip
        idx1++;
        idx2++;
        continue;
      }

      if (lines_equal(line1, line2, ignore_whitespace, ignore_all_whitespace)) {
        // Lines are equal
        output.push_back(format_line(line1, col_width) + "  " +
                         format_line(line2, col_width));
        idx1++;
        idx2++;
      } else {
        // Lines are different
        output.push_back(format_line(line1, col_width, '|') + "  " +
                         format_line(line2, col_width, '|'));
        idx1++;
        idx2++;
      }
    }
  }

  // Output
  for (const auto& line : output) {
    safePrintLn(line);
  }

  // Write to output file if specified
  if (!output_file.empty()) {
    std::wstring woutput = utf8_to_wstring(output_file);
    HANDLE hFile = CreateFileW(woutput.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("sdiff: cannot create output file '" + output_file +
                       "'");
      return 1;
    }

    std::string content;
    for (const auto& line : output) {
      content += line + "\r\n";
    }

    DWORD bytesWritten;
    WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()),
              &bytesWritten, nullptr);
    CloseHandle(hFile);
  }

  return 0;
}
