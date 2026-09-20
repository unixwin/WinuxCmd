// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

auto constexpr SDIFF_OPTIONS = std::array{
    // [GNU]
    OPTION("-o", "", "output file", STRING_TYPE),
    // [GNU]
    OPTION("-w", "", "set output width", STRING_TYPE),
    // [GNU]
    OPTION("-l", "", "print only the left column when lines are common"),
    // [GNU]
    OPTION("-s", "--suppress-common-lines", "do not print common lines"),
    // [GNU]
    OPTION("-B", "", "ignore changes whose lines are all blank"),
    // [GNU]
    OPTION("-E", "", "ignore tab expansion"),
    // [GNU]
    OPTION("-b", "", "ignore changes in amount of white space"),
    // [GNU]
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

// Expand tabs to spaces
std::string expand_tabs(const std::string& s) {
  std::string result;
  for (char c : s) {
    if (c == '\t') {
      result += "        ";  // 8 spaces per tab
    } else {
      result += c;
    }
  }
  return result;
}

// Compare lines with options
bool lines_equal(const std::string& line1, const std::string& line2,
                 bool ignore_whitespace, bool ignore_all_whitespace,
                 bool ignore_tab_expansion = false) {
  std::string l1 = ignore_tab_expansion ? expand_tabs(line1) : line1;
  std::string l2 = ignore_tab_expansion ? expand_tabs(line2) : line2;

  if (ignore_all_whitespace) {
    return remove_whitespace(l1) == remove_whitespace(l2);
  } else if (ignore_whitespace) {
    return trim_whitespace(l1) == trim_whitespace(l2);
  } else {
    return l1 == l2;
  }
}

size_t display_width(std::string_view text) {
  size_t width = 0;
  for (unsigned char c : text) {
    if (c == '\t') {
      width += 8 - (width % 8);
    } else {
      ++width;
    }
  }
  return width;
}

void append_padding_to_column(std::string& out, size_t& col, size_t target) {
  while (col < target) {
    size_t next_tab = col + (8 - (col % 8));
    if (next_tab <= target && next_tab > col) {
      out.push_back('\t');
      col = next_tab;
    } else {
      out.push_back(' ');
      ++col;
    }
  }
}

size_t marker_column_for_width(int width) {
  if (width <= 0) return 38;
  if (width == 40) return 19;
  if (width == 80) return 38;
  if (width == 130) return 62;
  return static_cast<size_t>(std::max(1, (width - 4) / 2));
}

size_t right_column_from_marker(size_t marker_col) {
  size_t after_marker = marker_col + 1;
  return after_marker + (8 - (after_marker % 8));
}

std::string format_common_line(const std::string& left,
                               const std::string& right, int output_width) {
  const size_t marker_col = marker_column_for_width(output_width);
  const size_t right_col = right_column_from_marker(marker_col);
  std::string out = left;
  size_t col = display_width(out);
  append_padding_to_column(out, col, right_col);
  out += right;
  return out;
}

std::string format_difference_line(const std::string& left,
                                   const std::string& right, int output_width,
                                   char marker) {
  const size_t marker_col = marker_column_for_width(output_width);
  std::string out = left;
  size_t col = display_width(out);
  append_padding_to_column(out, col, marker_col);
  out.push_back(marker);
  out.push_back('\t');
  out += right;
  return out;
}

auto resolve_files(const CommandContext<SDIFF_OPTIONS.size()>& ctx)
    -> std::expected<std::pair<std::string, std::string>, std::string> {
  std::vector<std::string> files;

  for (const auto& positional : ctx.positionals) {
    std::string file_arg = std::string(positional);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded && !glob_result.files.empty()) {
        for (const auto& file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }

    files.push_back(file_arg);
  }

  if (files.empty()) {
    return std::unexpected("missing operand");
  }
  if (files.size() < 2) {
    return std::unexpected("missing operand after '" + files.back() + "'");
  }
  if (files.size() > 2) {
    return std::unexpected("extra operand '" + files[2] + "'");
  }

  return std::make_pair(files[0], files[1]);
}

auto file_exists_for_read(const std::string& path) -> bool {
  std::error_code ec;
  return std::filesystem::exists(std::filesystem::u8path(path), ec) &&
         !std::filesystem::is_directory(std::filesystem::u8path(path), ec);
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
  bool ignore_whitespace = ctx.get<bool>("-b", false);
  bool ignore_all_whitespace = ctx.get<bool>("-W", false);
  bool ignore_blank = ctx.get<bool>("-B", false);
  bool ignore_tab_expansion = ctx.get<bool>("-E", false);
  bool left_only = ctx.get<bool>("-l", false);
  bool suppress_common = ctx.get<bool>("-s", false) ||
                         ctx.get<bool>("--suppress-common-lines", false);
  int output_width = 130;

  auto width_opt = ctx.get<std::string>("-w", "");
  if (!width_opt.empty()) {
    int parsed = 0;
    auto [ptr, ec] = std::from_chars(
        width_opt.data(), width_opt.data() + width_opt.size(), parsed);
    if (ec != std::errc() || ptr != width_opt.data() + width_opt.size() ||
        parsed <= 0) {
      safeErrorPrintLn("sdiff: invalid width '" + width_opt + "'");
      safeErrorPrint("Try 'sdiff --help' for more information.\n");
      return 1;
    }
    output_width = parsed;
  }

  auto files_result = resolve_files(ctx);
  if (!files_result) {
    safeErrorPrint("sdiff: ");
    safeErrorPrintLn(files_result.error());
    safeErrorPrint("Try 'sdiff --help' for more information.\n");
    return 1;
  }

  const auto& [file1, file2] = *files_result;

  // Read files
  std::vector<std::string> lines1 = read_file_lines(file1);
  std::vector<std::string> lines2 = read_file_lines(file2);

  if (lines1.empty() && !file_exists_for_read(file1)) {
    safeErrorPrintLn("sdiff: cannot read file '" + file1 + "'");
    return 1;
  }
  if (lines2.empty() && !file_exists_for_read(file2)) {
    safeErrorPrintLn("sdiff: cannot read file '" + file2 + "'");
    return 1;
  }

  // Compare lines side by side
  std::vector<std::string> output;
  std::vector<std::string> merged;
  bool any_difference = false;

  size_t idx1 = 0;
  size_t idx2 = 0;

  while (idx1 < lines1.size() || idx2 < lines2.size()) {
    if (idx1 >= lines1.size()) {
      // Only file2 has remaining lines.
      any_difference = true;
      merged.push_back(lines2[idx2]);
      output.push_back(
          format_difference_line("", lines2[idx2++], output_width, '>'));
    } else if (idx2 >= lines2.size()) {
      // Only file1 has remaining lines.
      any_difference = true;
      merged.push_back(lines1[idx1]);
      output.push_back(
          format_difference_line(lines1[idx1++], "", output_width, '<'));
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

      if (lines_equal(line1, line2, ignore_whitespace, ignore_all_whitespace,
                      ignore_tab_expansion)) {
        // Lines are equal
        merged.push_back(line1);
        if (suppress_common) {
          idx1++;
          idx2++;
          continue;
        }

        if (left_only) {
          output.push_back(line1);
        } else {
          output.push_back(format_common_line(line1, line2, output_width));
        }
        idx1++;
        idx2++;
      } else {
        // Lines are different.
        any_difference = true;
        // With -o, GNU sdiff's non-interactive EOF choice keeps the left
        // input line in the merged output.
        merged.push_back(line1);
        output.push_back(
            format_difference_line(line1, line2, output_width, '|'));
        idx1++;
        idx2++;
      }
    }
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
    for (const auto& line : merged) {
      content += line + "\n";
    }

    DWORD bytesWritten;
    WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()),
              &bytesWritten, nullptr);
    CloseHandle(hFile);
    for (const auto& line : output) safePrintLn(line);
    if (any_difference) safePrint("%");
  } else {
    for (const auto& line : output) safePrintLn(line);
  }

  // GNU reports differences through the exit status even when -o writes the
  // selected merge result successfully.
  return any_difference ? 1 : 0;
}
