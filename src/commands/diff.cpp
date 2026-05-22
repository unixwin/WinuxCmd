/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions.
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
 *  - File: diff.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for diff.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief DIFF command options definition
 *
 * This array defines all the options supported by the diff command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -q, @a --brief: Report only when files differ [IMPLEMENTED]
 * - @a -u, @a --unified: Output unified diff format [IMPLEMENTED]
 * - @a -y, @a --side-by-side: Output in two columns [IMPLEMENTED]
 * - @a -w, @a --ignore-all-space: Ignore all white space [IMPLEMENTED]
 * - @a
 * -B, @a --ignore-blank-lines: Ignore changes whose lines are all blank [NOT
 * SUPPORT]
 */
auto constexpr DIFF_OPTIONS =
    std::array{OPTION("-q", "--brief", "report only when files differ"),
               OPTION("-u", "--unified",
                      "output NUM (default 3) lines of unified context"),
               OPTION("-y", "--side-by-side", "output in two columns"),
               OPTION("-w", "--ignore-all-space", "ignore all white space"),
               OPTION("-B", "--ignore-blank-lines",
                      "ignore changes whose lines are all blank")};

namespace diff_pipeline {
namespace cp = core::pipeline;

auto resolve_files(const CommandContext<DIFF_OPTIONS.size()> &ctx)
    -> cp::Result<std::vector<std::string>> {
  std::vector<std::string> files;

  for (const auto &positional : ctx.positionals) {
    std::string file_arg = std::string(positional);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded && !glob_result.files.empty()) {
        for (const auto &file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }

    files.push_back(file_arg);
  }

  if (files.size() < 2) {
    return std::unexpected("missing operand");
  }
  if (files.size() > 2) {
    return std::unexpected("too many operands");
  }

  return files;
}

/**
 * @brief Edit operation type for diff
 */
enum class EditType { KEEP, DEL, INS };

/**
 * @brief Edit operation
 */
struct Edit {
  EditType type;
  size_t line1_index;  // Line index in file1 (for DEL/KEEP)
  size_t line2_index;  // Line index in file2 (for INS/KEEP)
};

/**
 * @brief Quick path: check if files are identical
 * @param lines1 Lines from first file
 * @param lines2 Lines from second file
 * @return true if files are identical
 */
auto is_identical(const std::vector<std::string> &lines1,
                  const std::vector<std::string> &lines2) -> bool {
  if (lines1.size() != lines2.size()) {
    return false;
  }
  return lines1 == lines2;
}

/**
 * @brief Compute LCS with hash optimization for fast comparison
 * @param lines1 Lines from first file
 * @param lines2 Lines from second file
 * @return LCS matrix
 */
auto compute_lcs_optimized(const std::vector<std::string> &lines1,
                           const std::vector<std::string> &lines2)
    -> std::vector<std::vector<size_t>> {
  size_t m = lines1.size();
  size_t n = lines2.size();

  // Fast path: if files are identical, no need to compute
  if (is_identical(lines1, lines2)) {
    std::vector<std::vector<size_t>> lcs(m + 1, std::vector<size_t>(n + 1, 0));
    for (size_t i = 0; i <= m; ++i) {
      lcs[i][i] = i;
    }
    return lcs;
  }

  // Precompute hashes for fast comparison
  std::vector<size_t> hash1;
  std::vector<size_t> hash2;
  hash1.reserve(m);
  hash2.reserve(n);

  for (const auto &line : lines1) {
    hash1.push_back(std::hash<std::string>{}(line));
  }
  for (const auto &line : lines2) {
    hash2.push_back(std::hash<std::string>{}(line));
  }

  // Create LCS matrix (needed for backtracking)
  std::vector<std::vector<size_t>> lcs(m + 1, std::vector<size_t>(n + 1, 0));

  for (size_t i = 1; i <= m; ++i) {
    for (size_t j = 1; j <= n; ++j) {
      // Compare hashes first, then confirm with string comparison
      if (hash1[i - 1] == hash2[j - 1] && lines1[i - 1] == lines2[j - 1]) {
        lcs[i][j] = lcs[i - 1][j - 1] + 1;
      } else {
        lcs[i][j] = std::max(lcs[i - 1][j], lcs[i][j - 1]);
      }
    }
  }

  return lcs;
}

/**
 * @brief Backtrack LCS matrix to find edit operations
 * @param lcs LCS matrix
 * @param lines1 Lines from first file
 * @param lines2 Lines from second file
 * @return Vector of edit operations
 */
auto backtrack_lcs(const std::vector<std::vector<size_t>> &lcs,
                   const std::vector<std::string> &lines1,
                   const std::vector<std::string> &lines2)
    -> std::vector<Edit> {
  std::vector<Edit> edits;
  size_t i = lines1.size();
  size_t j = lines2.size();

  while (i > 0 || j > 0) {
    if (i > 0 && j > 0 && lines1[i - 1] == lines2[j - 1]) {
      edits.push_back({EditType::KEEP, i - 1, j - 1});
      --i;
      --j;
    } else if (j > 0 && (i == 0 || lcs[i][j - 1] >= lcs[i - 1][j])) {
      edits.push_back({EditType::INS, i, j - 1});
      --j;
    } else {
      edits.push_back({EditType::DEL, i - 1, j});
      --i;
    }
  }

  std::reverse(edits.begin(), edits.end());
  return edits;
}

/**
 * @brief Compute diff using optimized LCS algorithm
 * @param lines1 Lines from first file
 * @param lines2 Lines from second file
 * @return Vector of edit operations
 */
auto compute_diff(const std::vector<std::string> &lines1,
                  const std::vector<std::string> &lines2) -> std::vector<Edit> {
  // Fast path: identical files
  if (is_identical(lines1, lines2)) {
    return {};
  }

  auto lcs = compute_lcs_optimized(lines1, lines2);
  return backtrack_lcs(lcs, lines1, lines2);
}

/**
 * @brief Read file into lines
 * @param path File path
 * @return Result with vector of lines
 */
auto read_file_lines_result(const std::string &path)
    -> cp::Result<std::vector<std::string>> {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::unexpected("cannot open '" + path + "' for reading");
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    // Remove carriage return if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }

  return lines;
}

auto normalize_line_for_compare(const std::string &line, bool ignore_all_space)
    -> std::string {
  if (!ignore_all_space) return line;
  std::string normalized;
  normalized.reserve(line.size());
  for (char ch : line) {
    if (!std::isspace(static_cast<unsigned char>(ch))) {
      normalized.push_back(ch);
    }
  }
  return normalized;
}

auto normalize_lines_for_compare(const std::vector<std::string> &lines,
                                 bool ignore_all_space)
    -> std::vector<std::string> {
  if (!ignore_all_space) return lines;
  std::vector<std::string> normalized;
  normalized.reserve(lines.size());
  for (const auto &line : lines) {
    normalized.push_back(normalize_line_for_compare(line, true));
  }
  return normalized;
}

/**
 * @brief Compare two files
 * @param path1 First file path
 * @param path2 Second file path
 * @param brief If true, only report if files differ
 * @return Result with true if files are equal
 */
auto compare_files(const std::string &path1, const std::string &path2,
                   bool brief, bool ignore_all_space) -> cp::Result<bool> {
  auto lines1_result = read_file_lines_result(path1);
  if (!lines1_result) {
    return std::unexpected(lines1_result.error());
  }

  auto lines2_result = read_file_lines_result(path2);
  if (!lines2_result) {
    return std::unexpected(lines2_result.error());
  }

  auto &lines1 = lines1_result.value();
  auto &lines2 = lines2_result.value();
  auto compare_lines1 = normalize_lines_for_compare(lines1, ignore_all_space);
  auto compare_lines2 = normalize_lines_for_compare(lines2, ignore_all_space);

  // Quick check: compare line counts
  if (compare_lines1.size() != compare_lines2.size()) {
    if (brief) {
      safePrint("Files ");
      safePrint(path1);
      safePrint(" and ");
      safePrint(path2);
      safePrint(" differ\n");
    }
    return false;
  }

  // Check if all lines are equal
  bool equal = true;
  for (size_t i = 0; i < compare_lines1.size(); ++i) {
    if (compare_lines1[i] != compare_lines2[i]) {
      equal = false;
      break;
    }
  }

  if (!equal && brief) {
    safePrint("Files ");
    safePrint(path1);
    safePrint(" and ");
    safePrint(path2);
    safePrint(" differ\n");
  }

  return equal;
}

/**
 * @brief Output unified diff format using LCS
 * @param path1 First file path
 * @param path2 Second file path
 * @param lines1 Lines from first file
 * @param lines2 Lines from second file
 * @param context Number of context lines
 */
auto output_unified_diff(const std::string &path1, const std::string &path2,
                         const std::vector<std::string> &compare_lines1,
                         const std::vector<std::string> &compare_lines2,
                         const std::vector<std::string> &lines1,
                         const std::vector<std::string> &lines2, int context)
    -> void {
  auto edits = compute_diff(compare_lines1, compare_lines2);

  // Group edits into hunks
  std::vector<std::pair<size_t, size_t>> hunks;  // (start_index, end_index)
  if (!edits.empty()) {
    size_t hunk_start = 0;
    for (size_t i = 1; i < edits.size(); ++i) {
      size_t distance = 0;

      // Calculate distance in terms of line numbers
      size_t prev_line1 = (edits[i - 1].type != EditType::INS)
                              ? edits[i - 1].line1_index
                              : (edits[i - 1].line1_index);
      size_t curr_line1 = (edits[i].type != EditType::INS)
                              ? edits[i].line1_index
                              : (edits[i].line1_index);
      size_t prev_line2 = (edits[i - 1].type != EditType::DEL)
                              ? edits[i - 1].line2_index
                              : (edits[i - 1].line2_index);
      size_t curr_line2 = (edits[i].type != EditType::DEL)
                              ? edits[i].line2_index
                              : (edits[i].line2_index);

      distance = std::max((curr_line1 > prev_line1 + context)
                              ? curr_line1 - prev_line1 - context
                              : 0,
                          (curr_line2 > prev_line2 + context)
                              ? curr_line2 - prev_line2 - context
                              : 0);

      if (distance > context * 2) {
        hunks.push_back({hunk_start, i});
        hunk_start = i;
      }
    }
    hunks.push_back({hunk_start, edits.size()});
  }

  if (hunks.empty()) {
    return;  // Files are identical
  }

  // Output header
  safePrint("--- ");
  safePrint(path1);
  safePrint("\n");
  safePrint("+++ ");
  safePrint(path2);
  safePrint("\n");

  // Output each hunk
  for (auto [hunk_start, hunk_end] : hunks) {
    // Find the range of lines in both files
    size_t file1_start = lines1.size();
    size_t file1_end = 0;
    size_t file2_start = lines2.size();
    size_t file2_end = 0;
    size_t context_start = lines1.size();
    size_t context_end = 0;

    for (size_t i = hunk_start; i < hunk_end; ++i) {
      const auto &edit = edits[i];

      if (edit.type == EditType::KEEP) {
        context_start = std::min(context_start, edit.line1_index);
        context_end = std::max(context_end, edit.line1_index + 1);
      } else if (edit.type == EditType::DEL) {
        file1_start = std::min(file1_start, edit.line1_index);
        file1_end = std::max(file1_end, edit.line1_index + 1);
      } else {  // INS
        file2_start = std::min(file2_start, edit.line2_index);
        file2_end = std::max(file2_end, edit.line2_index + 1);
      }
    }

    // Add context lines
    file1_start =
        std::max((ptrdiff_t)file1_start - (ptrdiff_t)context, (ptrdiff_t)0);
    file1_end = std::min(file1_end + context, lines1.size());
    file2_start =
        std::max((ptrdiff_t)file2_start - (ptrdiff_t)context, (ptrdiff_t)0);
    file2_end = std::min(file2_end + context, lines2.size());

    // Output hunk header
    safePrint("@@ -");
    safePrint(std::to_string(file1_start + 1));
    safePrint(",");
    safePrint(std::to_string(file1_end - file1_start));
    safePrint(" +");
    safePrint(std::to_string(file2_start + 1));
    safePrint(",");
    safePrint(std::to_string(file2_end - file2_start));
    safePrint(" @@\n");

    // Output hunk content
    size_t i1 = file1_start;
    size_t i2 = file2_start;

    for (size_t i = hunk_start; i < hunk_end; ++i) {
      const auto &edit = edits[i];

      if (edit.type == EditType::KEEP) {
        while (i1 < edit.line1_index && i1 < file1_end) {
          safePrint(" ");
          safePrint(lines1[i1]);
          safePrint("\n");
          ++i1;
          ++i2;
        }
        safePrint(" ");
        safePrint(lines1[edit.line1_index]);
        safePrint("\n");
        ++i1;
        ++i2;
      } else if (edit.type == EditType::DEL) {
        while (i1 < edit.line1_index && i1 < file1_end) {
          safePrint(" ");
          safePrint(lines1[i1]);
          safePrint("\n");
          ++i1;
          ++i2;
        }
        safePrint("-");
        safePrint(lines1[edit.line1_index]);
        safePrint("\n");
        ++i1;
      } else {  // INS
        while (i2 < edit.line2_index && i2 < file2_end) {
          safePrint(" ");
          safePrint(lines2[i2]);
          safePrint("\n");
          ++i1;
          ++i2;
        }
        safePrint("+");
        safePrint(lines2[edit.line2_index]);
        safePrint("\n");
        ++i2;
      }
    }

    // Output remaining context lines
    while (i1 < file1_end && i2 < file2_end) {
      safePrint(" ");
      safePrint(lines1[i1]);
      safePrint("\n");
      ++i1;
      ++i2;
    }
  }
}

/**
 * @brief Output side-by-side diff format
 * @param path1 First file path
 * @param path2 Second file path
 * @param lines1 Lines from first file
 * @param lines2 Lines from second file
 */
auto output_side_by_side(const std::string &path1, const std::string &path2,
                         const std::vector<std::string> &compare_lines1,
                         const std::vector<std::string> &compare_lines2,
                         const std::vector<std::string> &lines1,
                         const std::vector<std::string> &lines2) -> void {
  auto edits = compute_diff(compare_lines1, compare_lines2);

  if (edits.empty()) {
    return;  // Files are identical
  }

  const size_t col_width = 30;

  auto print_padded = [](const std::string &s, size_t width) {
    if (s.size() <= width) {
      safePrint(s);
      for (size_t i = s.size(); i < width; ++i) safePrint(" ");
    } else {
      safePrint(s.substr(0, width - 2));
      safePrint("..");
    }
  };

  size_t i1 = 0, i2 = 0;
  for (const auto &edit : edits) {
    if (edit.type == EditType::KEEP) {
      while (i1 < edit.line1_index) {
        print_padded(lines1[i1], col_width);
        safePrint("  ");
        print_padded(lines2[i2], col_width);
        safePrint("\n");
        ++i1;
        ++i2;
      }
      print_padded(lines1[edit.line1_index], col_width);
      safePrint("  ");
      print_padded(lines2[edit.line2_index], col_width);
      safePrint("\n");
      ++i1;
      ++i2;
    } else if (edit.type == EditType::DEL) {
      print_padded(lines1[edit.line1_index], col_width);
      safePrint(" +");
      for (size_t p = 0; p < col_width; ++p) safePrint(" ");
      safePrint("\n");
      ++i1;
    } else {
      for (size_t p = 0; p < col_width; ++p) safePrint(" ");
      safePrint(" +");
      print_padded(lines2[edit.line2_index], col_width);
      safePrint("\n");
      ++i2;
    }
  }

  while (i1 < lines1.size()) {
    print_padded(lines1[i1], col_width);
    safePrint("  ");
    for (size_t p = 0; p < col_width; ++p) safePrint(" ");
    safePrint("\n");
    ++i1;
  }
  while (i2 < lines2.size()) {
    for (size_t p = 0; p < col_width; ++p) safePrint(" ");
    safePrint("  ");
    print_padded(lines2[i2], col_width);
    safePrint("\n");
    ++i2;
  }
}

}  // namespace diff_pipeline

REGISTER_COMMAND(
    diff, "diff", "compare files line by line",
    "Compare files line by line and report differences.\n"
    "\n"
    "This is a simplified implementation of the Unix diff utility.\n"
    "It supports basic comparison and unified diff output.",
    "  diff file1 file2         Compare two files\n"
    "  diff -q file1 file2      Only report if files differ\n"
    "  diff -u file1 file2      Show unified diff format",
    "cmp(1), patch(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    DIFF_OPTIONS) {
  using namespace diff_pipeline;

  bool brief = ctx.get<bool>("-q", false) || ctx.get<bool>("--brief", false);
  bool unified =
      ctx.get<bool>("-u", false) || ctx.get<bool>("--unified", false);
  bool side_by_side =
      ctx.get<bool>("-y", false) || ctx.get<bool>("--side-by-side", false);
  bool ignore_all_space =
      ctx.get<bool>("-w", false) || ctx.get<bool>("--ignore-all-space", false);
  int context = 3;  // Default context lines for unified diff

  auto files_result = resolve_files(ctx);
  if (!files_result) {
    safeErrorPrint("diff: ");
    safeErrorPrintLn(files_result.error());
    safeErrorPrint("Try 'diff --help' for more information.\n");
    return 1;
  }

  std::string file1 = (*files_result)[0];
  std::string file2 = (*files_result)[1];

  if (brief) {
    auto result = compare_files(file1, file2, true, ignore_all_space);
    if (!result) {
      safeErrorPrint("diff: ");
      safeErrorPrint(result.error());
      safeErrorPrint("\n");
      return 1;
    }
    return result.value() ? 0 : 1;
  }

  // Read both files
  auto lines1_result = read_file_lines_result(file1);
  if (!lines1_result) {
    safeErrorPrint("diff: ");
    safeErrorPrint(lines1_result.error());
    safeErrorPrint("\n");
    return 1;
  }

  auto lines2_result = read_file_lines_result(file2);
  if (!lines2_result) {
    safeErrorPrint("diff: ");
    safeErrorPrint(lines2_result.error());
    safeErrorPrint("\n");
    return 1;
  }

  auto &lines1 = lines1_result.value();
  auto &lines2 = lines2_result.value();
  auto compare_lines1 = normalize_lines_for_compare(lines1, ignore_all_space);
  auto compare_lines2 = normalize_lines_for_compare(lines2, ignore_all_space);

  if (compare_lines1 == compare_lines2) {
    return 0;
  }

  if (unified) {
    output_unified_diff(file1, file2, compare_lines1, compare_lines2, lines1,
                        lines2, context);
  } else if (side_by_side) {
    output_side_by_side(file1, file2, compare_lines1, compare_lines2, lines1,
                        lines2);
  } else {
    // Simple comparison using LCS
    auto edits = compute_diff(compare_lines1, compare_lines2);

    bool has_diffs = false;
    for (const auto &edit : edits) {
      if (edit.type == EditType::DEL) {
        safePrint("< ");
        safePrint(lines1[edit.line1_index]);
        safePrint("\n");
        has_diffs = true;
      } else if (edit.type == EditType::INS) {
        safePrint("> ");
        safePrint(lines2[edit.line2_index]);
        safePrint("\n");
        has_diffs = true;
      }
    }

    if (!has_diffs) {
      // Files are identical
      return 0;
    }
  }

  return 1;
}
