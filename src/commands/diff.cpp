// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for diff.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include <shellapi.h>
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
// [GNU] -a, --text: treat all files as text
// [GNU] -b, --ignore-space-change: ignore changes in amount of white space
// [GNU] --binary: read and write data as binary
// [GNU] --color: colorize the output
// [GNU] -d, --minimal: try hard to find a smaller set of changes
// [GNU] --diff-program=PROGRAM: use PROGRAM to compare files
// [GNU] -e, --ed: output an ed script
// [GNU] --exclude=PATTERN: exclude files that match PATTERN
// [GNU] --exclude-from=FILE: exclude files that match any pattern in FILE
// [GNU] --exclude-dir=PATTERN: exclude directories that match PATTERN
// [GNU] -f, --forward-ed: output an ed script for current changes
// [GNU] -i, --ignore-case: ignore case when comparing files
// [GNU] --ignore-file-name-case: ignore case when comparing file names
// [GNU] -I, --ignore-matching-lines=NUM: ignore changes that match NUM lines
// [GNU] -l, --paginate: pass output through pr
// [GNU] -n, --rcs: output an RCS-format diff
// [GNU] -N, --new-file: treat absent files as empty
// [GNU] --no-dereference: don't follow symbolic links
// [GNU] -p, --show-c-function: show which C function each change is in
// [GNU] --show-function-line=REGEXP: show the most recent line matching REGEXP
// [GNU] -r, --recursive: recursively compare any subdirectories found
// [GNU] --report-identical-files: report when two files are the same
// [GNU] -s, --report-identical-files: report when two files are the same
// [GNU] --strip-trailing-cr: strip trailing carriage return on input
// [GNU] -S, --starting-file=FILE: start with FILE when comparing directories
// [GNU] --suppress-blank-empty: suppress empty common lines
// [GNU] --suppress-common-lines: suppress common lines in side-by-side format
// [GNU] -t, --expand-tabs: expand tabs to spaces in output
// [GNU] -T, --initial-tab: tab stop every NUM output lines
// [GNU] --tabsize=NUM: tab stop every NUM (default 8) print positions
// [GNU] --unidirectional-new-file: treat absent first files as empty
// [GNU] -W, --width=NUM: output at most NUM (default 130) columns
// [GNU] -x, --exclude=PAT: exclude files that match PAT
// [GNU] -X, --exclude-from=FILE: exclude files that match any pattern in FILE
// [GNU] -Z, --strip-trailing-cr: strip trailing carriage return on input
auto constexpr DIFF_OPTIONS = std::array{
    // [GNU 3.10] wording follows `diff --help` verbatim.
    OPTION("", "--normal", "output a normal diff (the default)"),
    OPTION("-q", "--brief", "report only when files differ"),
    OPTION("-s", "--report-identical-files",
           "report when two files are the same"),
    OPTION("-c", "--context", "output NUM (default 3) lines of copied context",
           OPTIONAL_INT_TYPE),
    OPTION("-C", "", "output NUM lines of copied context", INT_TYPE),
    OPTION("-u", "--unified", "output NUM (default 3) lines of unified context",
           OPTIONAL_INT_TYPE),
    OPTION("-U", "", "output NUM lines of unified context", INT_TYPE),
    OPTION("-e", "--ed", "output an ed script"),
    OPTION("-f", "--forward-ed",
           "output an ed script for the second file"),
    OPTION("-n", "--rcs", "output an RCS format diff"),
    OPTION("-y", "--side-by-side", "output in two columns"),
    OPTION("-W", "--width", "output at most NUM (default 130) print columns",
           INT_TYPE),
    OPTION("", "--left-column", "output only the left column of common lines"),
    OPTION("", "--suppress-common-lines", "do not output common lines"),
    OPTION("-p", "--show-c-function",
           "show which C function each change is in"),
    OPTION("-F", "--show-function-line",
           "show the most recent line matching RE", STRING_TYPE),
    OPTION("", "--label", "use LABEL instead of file name and timestamp",
           STRING_TYPE),
    OPTION("-t", "--expand-tabs", "expand tabs to spaces in output"),
    OPTION("-T", "--initial-tab",
           "make tabs line up by prepending a tab"),
    OPTION("", "--tabsize", "tab stops every NUM (default 8) print columns",
           INT_TYPE),
    OPTION("", "--suppress-blank-empty",
           "suppress space or tab before empty output lines"),
    OPTION("-l", "--paginate", "pass output through 'pr' to paginate it"),
    OPTION("-r", "--recursive",
           "recursively compare any subdirectories found"),
    OPTION("", "--no-dereference", "don't follow symbolic links"),
    OPTION("-N", "--new-file", "treat absent files as empty"),
    OPTION("", "--unidirectional-new-file",
           "treat absent first files as empty"),
    OPTION("", "--ignore-file-name-case",
           "ignore case when comparing file names"),
    OPTION("", "--no-ignore-file-name-case",
           "consider case when comparing file names"),
    OPTION("-x", "", "exclude files that match PAT", STRING_TYPE),
    OPTION("-X", "", "exclude files that match any pattern in FILE",
           STRING_TYPE),
    OPTION("-S", "", "start with FILE when comparing directories",
           STRING_TYPE),
    OPTION("", "--from-file", "compare FILE1 to all operands; FILE1 can be a "
                              "directory",
           STRING_TYPE),
    OPTION("", "--to-file", "compare all operands to FILE2; FILE2 can be a "
                            "directory",
           STRING_TYPE),
    OPTION("-i", "--ignore-case", "ignore case differences in file contents"),
    OPTION("-E", "--ignore-tab-expansion",
           "ignore changes due to tab expansion"),
    OPTION("-Z", "--ignore-trailing-space",
           "ignore white space at line end"),
    OPTION("-b", "--ignore-space-change",
           "ignore changes in the amount of white space"),
    OPTION("-w", "--ignore-all-space", "ignore all white space"),
    OPTION("-B", "--ignore-blank-lines",
           "ignore changes where lines are all blank"),
    OPTION("-I", "--ignore-matching-lines",
           "ignore changes where all lines match RE", STRING_TYPE),
    OPTION("-a", "--text", "treat all files as text"),
    OPTION("", "--strip-trailing-cr",
           "strip trailing carriage return on input"),
    OPTION("-D", "--ifdef",
           "output merged file with '#ifdef NAME' diffs", STRING_TYPE),
    OPTION("", "--old-group-format", "format changed groups of lines from "
                                     "FILE1 with GFMT",
           STRING_TYPE),
    OPTION("", "--new-group-format", "format changed groups of lines from "
                                     "FILE2 with GFMT",
           STRING_TYPE),
    OPTION("", "--changed-group-format",
           "format a group of differing lines with GFMT", STRING_TYPE),
    OPTION("", "--unchanged-group-format",
           "format a group of unchanged lines with GFMT", STRING_TYPE),
    OPTION("", "--line-format", "format all input lines with LFMT",
           STRING_TYPE),
    OPTION("", "--old-line-format", "format lines from FILE1 with LFMT",
           STRING_TYPE),
    OPTION("", "--new-line-format", "format lines from FILE2 with LFMT",
           STRING_TYPE),
    OPTION("", "--unchanged-line-format",
           "format common lines with LFMT", STRING_TYPE),
    OPTION("-d", "--minimal",
           "try hard to find a smaller set of changes (this diff is always "
           "minimal)"),
    OPTION("", "--horizon-lines",
           "keep NUM lines of the common prefix and suffix", INT_TYPE),
    OPTION("", "--speed-large-files",
           "assume large files and many scattered small changes"),
    OPTION("", "--color", "color output; WHEN is 'never', 'always', or "
                          "'auto'; plain --color means --color='auto'",
           OPTIONAL_STRING_TYPE),
    OPTION("", "--palette",
           "the colors to use when --color is active (accepted, default "
           "palette used)"),
    OPTION("", "--binary", "read and write data as binary"),
    OPTION("", "--diff-program", "use PROGRAM to compare files (accepted, "
                                 "built-in engine used)",
           STRING_TYPE),
    OPTION("", "--exclude", "alias for -x, exclude files that match PATTERN",
           STRING_TYPE),
    OPTION("", "--exclude-from",
           "alias for -X, exclude files matching pattern in FILE",
           STRING_TYPE),
    OPTION("", "--exclude-dir", "exclude directories matching PATTERN",
           STRING_TYPE)};

namespace diff_pipeline {
namespace cp = core::pipeline;

// ===== GNU diffutils 3.10 output engines: ed / RCS / format directives =====

// Output sink: -l/--paginate buffers the diff and paginates it pr-style
// (66-line pages with a dated header) instead of writing each line out.
std::string paginate_buffer;
bool buffering_output = false;

void dput(std::string_view s) {
  if (buffering_output) {
    paginate_buffer.append(s);
  } else {
    safePrint(s);
  }
}

struct DiffColors {
  bool on = false;
  [[nodiscard]] auto bold(std::string_view s) const -> std::string {
    return on ? "\033[1m" + std::string(s) + "\033[0m" : std::string(s);
  }
  [[nodiscard]] auto cyan(std::string_view s) const -> std::string {
    return on ? "\033[36m" + std::string(s) + "\033[0m" : std::string(s);
  }
  [[nodiscard]] auto red(std::string_view s) const -> std::string {
    return on ? "\033[31m" + std::string(s) + "\033[0m" : std::string(s);
  }
  [[nodiscard]] auto green(std::string_view s) const -> std::string {
    return on ? "\033[32m" + std::string(s) + "\033[0m" : std::string(s);
  }
};

// Styling shared by the line printers (--color palette, -p/-F function
// context). A default-constructed value is "everything off".
struct StyleCtx {
  const DiffColors *colors = nullptr;
  const portable_regex::Pattern *func_re = nullptr;
  bool func_show_c = false;  // -p default: leading alphabetic/underscore/$
};


auto find_function_line(const std::vector<std::string> &lines,
                        size_t bound,
                        const portable_regex::Pattern *func_re,
                        bool func_show_c) -> std::string;

// Reconstructs the "diff -ru a b" tail GNU echoes in -r headers and the
// -l page header: argv[0] dropped, plus the subcommand token when
// dispatched through the winuxcmd multiplexer.
auto diff_command_tail() -> std::string {
  int argc = 0;
  LPWSTR *argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv_w || argc < 1) return "diff";
  std::string tail = "diff";
  for (int i = 1; i < argc; ++i) {
    std::string token = wstring_to_utf8(argv_w[i]);
    if (i == 1 && token == "diff") continue;
    tail += " ";
    tail += token;
  }
  LocalFree(argv_w);
  return tail;
}

auto split_lines(const std::string &text) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::string current;
  for (char ch : text) {
    if (ch == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current += ch;
    }
  }
  return lines;
}

// pr(1)-style pagination: 72-column header line = date (18) + centered
// title (45) + right-aligned "Page N" (9); 66 lines per page with a
// two-blank/header/two-blank masthead. Form feed only between pages.
void paginate_and_flush(const std::string &title) {
  auto lines = split_lines(paginate_buffer);
  const size_t kPageLines = 66;
  const size_t kBodyLines = kPageLines - 5;
  size_t page = 1;
  for (size_t i = 0; i < lines.size() || page == 1; i += kBodyLines, ++page) {
    if (page > 1) dput("\f\n");
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char datebuf[32]{};
    std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d %02d:%02d",
                  static_cast<int>(st.wYear), static_cast<int>(st.wMonth),
                  static_cast<int>(st.wDay), static_cast<int>(st.wHour),
                  static_cast<int>(st.wMinute));
    char pagebuf[24]{};
    std::snprintf(pagebuf, sizeof(pagebuf), "Page %zu", page);
    std::string page_str(pagebuf);
    const size_t middle = 72 - 18 - page_str.size();
    const size_t pad = title.size() < middle ? (middle - title.size()) / 2 : 0;
    dput("\n\n");
    dput(datebuf);
    dput(std::string(18 - std::strlen(datebuf), ' '));
    dput(std::string(pad, ' ') + title + "\n\n");
    size_t emitted = 0;
    for (size_t j = i; j < lines.size() && emitted < kBodyLines; ++j, ++emitted) {
      dput(lines[j] + "\n");
    }
    for (size_t j = emitted; j < kBodyLines; ++j) dput("\n");
    if (i >= lines.size()) break;
  }
  paginate_buffer.clear();
  buffering_output = false;
}

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

  if (files.empty()) {
    return std::unexpected("missing operand");
  }
  if (files.size() < 2) {
    return std::unexpected("missing operand after '" + files.back() + "'");
  }
  if (files.size() > 2) {
    return std::unexpected("extra operand '" + files[2] + "'");
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
// [GNU] "-" (and /dev/stdin-family operands) read standard input (#1057).
auto operand_is_stdin(const std::string &path) -> bool {
  return path == "-" ||
         native_path::pseudo_device_std_fd(path) == std::optional<int>(0);
}

auto read_file_lines_result(const std::string &path)
    -> cp::Result<std::vector<std::string>> {
  auto diff_input_open_error = [](std::string_view file_path) -> std::string {
    auto operand = native_path::make_api_path_operand(file_path);
    const DWORD attrs = native_path::operand_target_attributes_w(operand);
    if (native_path::attributes_are_directory(attrs)) {
      return std::string(file_path) + ": Is a directory";
    }
    // [GNU] Unreadable input reports "<path>: <errno text>", not a
    // "cannot open ... for reading" wrapper.
    return std::string(file_path) + ": No such file or directory";
  };

  std::vector<std::string> lines;

  if (operand_is_stdin(path)) {
    // [GNU] A closed standard input (<&-) is a read error, not EOF (#973).
    // For "-" GNU reports EBADF; a /dev/stdin-family operand dangles like a
    // dead /proc/self/fd symlink and reports ENOENT instead.
    if (file_io::stdin_is_bad()) {
      return std::unexpected(path + (path == "-"
                                         ? ": Bad file descriptor"
                                         : ": No such file or directory"));
    }
    std::string line;
    while (std::getline(std::cin, line)) {
      lines.push_back(line);
    }
    return lines;
  }

  std::ifstream file = file_io::open_binary_file(path);
  if (!file.is_open()) {
    return std::unexpected(diff_input_open_error(path));
  }

  std::string line;
  while (std::getline(file, line)) {
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

auto strip_trailing_cr_lines(const std::vector<std::string> &lines)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  result.reserve(lines.size());
  for (const auto &line : lines) {
    if (!line.empty() && line.back() == '\r') {
      result.emplace_back(line.substr(0, line.size() - 1));
    } else {
      result.push_back(line);
    }
  }
  return result;
}

auto filter_blank_lines(const std::vector<std::string> &lines)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  for (const auto &line : lines) {
    bool all_space = true;
    for (char ch : line) {
      if (!std::isspace(static_cast<unsigned char>(ch))) {
        all_space = false;
        break;
      }
    }
    if (!all_space) {
      result.push_back(line);
    }
  }
  return result;
}

auto normalize_space_change(const std::string &line) -> std::string {
  std::string result;
  result.reserve(line.size());
  bool in_space = false;
  for (char ch : line) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!in_space) {
        result.push_back(' ');
        in_space = true;
      }
    } else {
      result.push_back(ch);
      in_space = false;
    }
  }
  return result;
}

auto normalize_lines_space_change(const std::vector<std::string> &lines)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  result.reserve(lines.size());
  for (const auto &line : lines) {
    result.push_back(normalize_space_change(line));
  }
  return result;
}

auto normalize_lines_case(const std::vector<std::string> &lines)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  result.reserve(lines.size());
  for (const auto &line : lines) {
    std::string lowered;
    lowered.reserve(line.size());
    for (char ch : line) {
      lowered.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    result.push_back(std::move(lowered));
  }
  return result;
}

auto filter_matching_lines(const std::vector<std::string> &lines,
                           const std::string &pattern)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  for (const auto &line : lines) {
    if (line.find(pattern) == std::string::npos) {
      result.push_back(line);
    }
  }
  return result;
}

auto expand_tabs_in_line(const std::string &line, int tabsize) -> std::string {
  std::string result;
  int col = 0;
  for (char ch : line) {
    if (ch == '\t') {
      int spaces = tabsize - (col % tabsize);
      result.append(spaces, ' ');
      col += spaces;
    } else {
      result.push_back(ch);
      ++col;
    }
  }
  return result;
}

auto expand_tabs_lines(const std::vector<std::string> &lines, int tabsize)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  result.reserve(lines.size());
  for (const auto &line : lines) {
    result.push_back(expand_tabs_in_line(line, tabsize));
  }
  return result;
}

/**
 * @brief Compare two files
 * @param path1 First file path
 * @param path2 Second file path
 * @param brief If true, only report if files differ
 * @return Result with true if files are equal
 */
auto compare_files(const std::string &path1, const std::string &path2,
                   const std::vector<std::string> &lines1,
                   const std::vector<std::string> &lines2, bool brief,
                   bool ignore_all_space) -> bool {
  auto compare_lines1 = normalize_lines_for_compare(lines1, ignore_all_space);
  auto compare_lines2 = normalize_lines_for_compare(lines2, ignore_all_space);

  // Quick check: compare line counts
  if (compare_lines1.size() != compare_lines2.size()) {
    if (brief) {
      dput("Files ");
      dput(path1);
      dput(" and ");
      dput(path2);
      dput(" differ\n");
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
    dput("Files ");
    dput(path1);
    dput(" and ");
    dput(path2);
    dput(" differ\n");
  }

  return equal;
}

auto filetime_ticks(FILETIME ft) -> uint64_t {
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

auto format_unified_timestamp(const std::string &path) -> std::string {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(utf8_to_wstring(path).c_str(),
                            GetFileExInfoStandard, &data)) {
    return "";
  }

  FILETIME utc_ft = data.ftLastWriteTime;
  FILETIME local_ft{};
  if (!FileTimeToLocalFileTime(&utc_ft, &local_ft)) {
    local_ft = utc_ft;
  }

  SYSTEMTIME st{};
  FileTimeToSystemTime(&local_ft, &st);

  int64_t offset_ticks = static_cast<int64_t>(filetime_ticks(local_ft)) -
                         static_cast<int64_t>(filetime_ticks(utc_ft));
  int offset_minutes = static_cast<int>(offset_ticks / (10'000'000LL * 60LL));
  char sign = '+';
  if (offset_minutes < 0) {
    sign = '-';
    offset_minutes = -offset_minutes;
  }

  uint64_t fractional_ns = (filetime_ticks(local_ft) % 10'000'000ULL) * 100ULL;
  char buf[96]{};
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%09llu %c%02d%02d",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
           static_cast<unsigned long long>(fractional_ns), sign,
           offset_minutes / 60, offset_minutes % 60);
  return std::string(buf);
}

auto format_unified_range(size_t start_line, size_t count) -> std::string {
  std::string out = std::to_string(start_line);
  if (count != 1) {
    out += ",";
    out += std::to_string(count);
  }
  return out;
}

auto format_context_range(size_t start_line, size_t count) -> std::string {
  if (count == 1) return std::to_string(start_line);
  if (count == 0) return std::to_string(start_line) + ",0";
  return std::to_string(start_line) + "," +
         std::to_string(start_line + count - 1);
}

struct DiffHunk {
  size_t edit_start = 0;
  size_t edit_end = 0;
  size_t file1_start = 0;
  size_t file1_end = 0;
  size_t file2_start = 0;
  size_t file2_end = 0;
};

auto build_diff_hunks(const std::vector<Edit> &edits, size_t line1_count,
                      size_t line2_count, int context)
    -> std::vector<DiffHunk> {
  std::vector<DiffHunk> hunks;
  if (edits.empty()) return hunks;

  auto context_lines = static_cast<size_t>(std::max(context, 0));
  std::vector<std::pair<size_t, size_t>> groups;
  if (context_lines == 0) {
    // GNU normal format: one hunk per maximal run of non-KEEP edits. The
    // distance heuristic below cannot be used with zero context because
    // DEL/INS records carry cross-file anchors that make adjacent change
    // pairs look farther apart than they are.
    bool in_run = false;
    size_t run_start = 0;
    for (size_t i = 0; i < edits.size(); ++i) {
      if (edits[i].type == EditType::KEEP) {
        if (in_run) {
          groups.push_back({run_start, i});
          in_run = false;
        }
      } else if (!in_run) {
        run_start = i;
        in_run = true;
      }
    }
    if (in_run) groups.push_back({run_start, edits.size()});
  } else {
    // [GNU] Hunks merge while the common-line gap between consecutive
    // CHANGES is at most 2*context; distance is measured change-to-change
    // (KEEP runs in between are exactly the gap being measured).
    size_t hunk_start = 0;
    bool have_prev_change = false;
    size_t prev_change1 = 0;
    size_t prev_change2 = 0;
    for (size_t i = 0; i < edits.size(); ++i) {
      if (edits[i].type == EditType::KEEP) continue;
      if (have_prev_change) {
        size_t gap1 = edits[i].line1_index > prev_change1 + 1
                          ? edits[i].line1_index - prev_change1 - 1
                          : 0;
        size_t gap2 = edits[i].line2_index > prev_change2 + 1
                          ? edits[i].line2_index - prev_change2 - 1
                          : 0;
        size_t gap = std::max(gap1, gap2);
        if (gap > static_cast<size_t>(context_lines) * 2) {
          groups.push_back({hunk_start, i});
          hunk_start = i;
        }
      }
      have_prev_change = true;
      prev_change1 = edits[i].line1_index;
      prev_change2 = edits[i].line2_index;
    }
    groups.push_back({hunk_start, edits.size()});
  }

  for (auto [group_start, group_end] : groups) {
    DiffHunk hunk;
    hunk.edit_start = group_start;
    hunk.edit_end = group_end;
    hunk.file1_start = line1_count;
    hunk.file1_end = 0;
    hunk.file2_start = line2_count;
    hunk.file2_end = 0;

    for (size_t i = group_start; i < group_end; ++i) {
      const auto &edit = edits[i];
      if (edit.type == EditType::DEL) {
        hunk.file1_start = std::min(hunk.file1_start, edit.line1_index);
        hunk.file1_end = std::max(hunk.file1_end, edit.line1_index + 1);
      } else if (edit.type == EditType::INS) {
        hunk.file2_start = std::min(hunk.file2_start, edit.line2_index);
        hunk.file2_end = std::max(hunk.file2_end, edit.line2_index + 1);
      }
    }

    if (hunk.file1_start == line1_count) {
      hunk.file1_start = edits[group_start].line1_index;
      hunk.file1_end = hunk.file1_start;
    }
    if (hunk.file2_start == line2_count) {
      hunk.file2_start = edits[group_start].line2_index;
      hunk.file2_end = hunk.file2_start;
    }

    hunk.file1_start =
        static_cast<size_t>(std::max(static_cast<ptrdiff_t>(hunk.file1_start) -
                                         static_cast<ptrdiff_t>(context_lines),
                                     static_cast<ptrdiff_t>(0)));
    hunk.file1_end = std::min(hunk.file1_end + context_lines, line1_count);
    hunk.file2_start =
        static_cast<size_t>(std::max(static_cast<ptrdiff_t>(hunk.file2_start) -
                                         static_cast<ptrdiff_t>(context_lines),
                                     static_cast<ptrdiff_t>(0)));
    hunk.file2_end = std::min(hunk.file2_end + context_lines, line2_count);

    hunks.push_back(hunk);
  }

  return hunks;
}

struct DiffLabel {
  std::string text;
  bool custom = false;
};

// Styling shared by the line printers (--color palette, -p/-F function
// context). A default-constructed value is "everything off".

void output_diff_file_header(std::string_view prefix, const DiffLabel &label,
                             const std::string &path,
                             const StyleCtx &style = {}) {
  std::string line(prefix);
  line += label.text;
  if (!label.custom) {
    auto timestamp = format_unified_timestamp(path);
    if (!timestamp.empty()) {
      line += "\t";
      line += timestamp;
    }
  }
  dput(style.colors ? style.colors->bold(line) : line);
  dput("\n");
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
                         const std::vector<std::string> &lines2, int context,
                         const DiffLabel &label1, const DiffLabel &label2,
                         const StyleCtx &style = {})
    -> void {
  auto edits = compute_diff(compare_lines1, compare_lines2);
  auto hunks = build_diff_hunks(edits, lines1.size(), lines2.size(), context);
  if (hunks.empty()) return;

  output_diff_file_header("--- ", label1, path1, style);
  output_diff_file_header("+++ ", label2, path2, style);

  // GNU -p/-F: the function header is the most recent matching line
  // before each hunk, tracked independently per hunk start.
  auto hunk_function = [&](const DiffHunk &hunk) -> std::string {
    if (!style.func_re && !style.func_show_c) return {};
    return find_function_line(lines1, hunk.file1_start, style.func_re,
                              style.func_show_c);
  };

  const DiffColors &colors =
      style.colors ? *style.colors : DiffColors{};
  for (const auto &hunk : hunks) {
    std::string header = "@@ -";
    header += format_unified_range(hunk.file1_start + 1,
                                   hunk.file1_end - hunk.file1_start);
    header += " +";
    header += format_unified_range(hunk.file2_start + 1,
                                   hunk.file2_end - hunk.file2_start);
    header += " @@";
    std::string func = hunk_function(hunk);
    if (!func.empty()) header += " " + func;
    dput(colors.cyan(header));
    dput("\n");

    auto emit = [&](std::string_view prefix, const std::string &line) {
      if (prefix == "-") {
        dput(colors.red(std::string(prefix) + line));
      } else if (prefix == "+") {
        dput(colors.green(std::string(prefix) + line));
      } else {
        dput(std::string(prefix));
        dput(line);
      }
      dput("\n");
    };

    size_t i1 = hunk.file1_start;
    size_t i2 = hunk.file2_start;
    for (size_t i = hunk.edit_start; i < hunk.edit_end; ++i) {
      const auto &edit = edits[i];

      if (edit.type == EditType::KEEP) {
        if (edit.line1_index < hunk.file1_start ||
            edit.line1_index >= hunk.file1_end ||
            edit.line2_index < hunk.file2_start ||
            edit.line2_index >= hunk.file2_end) {
          continue;
        }
        while (i1 < edit.line1_index && i1 < hunk.file1_end) {
          emit(" ", lines1[i1]);
          ++i1;
          ++i2;
        }
        emit(" ", lines1[edit.line1_index]);
        ++i1;
        ++i2;
      } else if (edit.type == EditType::DEL) {
        if (edit.line1_index < hunk.file1_start ||
            edit.line1_index >= hunk.file1_end) {
          continue;
        }
        while (i1 < edit.line1_index && i1 < hunk.file1_end) {
          emit(" ", lines1[i1]);
          ++i1;
          ++i2;
        }
        emit("-", lines1[edit.line1_index]);
        ++i1;
      } else {
        if (edit.line2_index < hunk.file2_start ||
            edit.line2_index >= hunk.file2_end) {
          continue;
        }
        while (i2 < edit.line2_index && i2 < hunk.file2_end) {
          emit(" ", lines2[i2]);
          ++i1;
          ++i2;
        }
        emit("+", lines2[edit.line2_index]);
        ++i2;
      }
    }

    while (i1 < hunk.file1_end && i2 < hunk.file2_end) {
      emit(" ", lines1[i1]);
      ++i1;
      ++i2;
    }
  }
}

void output_context_old_section(const std::vector<Edit> &edits,
                                const std::vector<std::string> &lines1,
                                const DiffHunk &hunk, bool mixed_change,
                                const DiffColors &colors = {}) {
  size_t i1 = hunk.file1_start;
  for (size_t i = hunk.edit_start; i < hunk.edit_end; ++i) {
    const auto &edit = edits[i];
    if (edit.type == EditType::INS) continue;
    if (edit.type == EditType::KEEP) {
      if (edit.line1_index < hunk.file1_start ||
          edit.line1_index >= hunk.file1_end) {
        continue;
      }
      while (i1 < edit.line1_index && i1 < hunk.file1_end) {
        dput("  ");
        dput(lines1[i1]);
        dput("\n");
        ++i1;
      }
      dput("  ");
      dput(lines1[edit.line1_index]);
      dput("\n");
      ++i1;
      continue;
    }
    if (edit.line1_index < hunk.file1_start ||
        edit.line1_index >= hunk.file1_end) {
      continue;
    }
    while (i1 < edit.line1_index && i1 < hunk.file1_end) {
      dput("  ");
      dput(lines1[i1]);
      dput("\n");
      ++i1;
    }
    dput(mixed_change ? "! " : "- ");
    dput(lines1[edit.line1_index]);
    dput("\n");
    ++i1;
  }
  while (i1 < hunk.file1_end) {
    dput("  ");
    dput(lines1[i1]);
    dput("\n");
    ++i1;
  }
}

void output_context_new_section(const std::vector<Edit> &edits,
                                const std::vector<std::string> &lines2,
                                const DiffHunk &hunk, bool mixed_change,
                                const DiffColors &colors = {}) {
  size_t i2 = hunk.file2_start;
  for (size_t i = hunk.edit_start; i < hunk.edit_end; ++i) {
    const auto &edit = edits[i];
    if (edit.type == EditType::DEL) continue;
    if (edit.type == EditType::KEEP) {
      if (edit.line2_index < hunk.file2_start ||
          edit.line2_index >= hunk.file2_end) {
        continue;
      }
      while (i2 < edit.line2_index && i2 < hunk.file2_end) {
        dput("  ");
        dput(lines2[i2]);
        dput("\n");
        ++i2;
      }
      dput("  ");
      dput(lines2[edit.line2_index]);
      dput("\n");
      ++i2;
      continue;
    }
    if (edit.line2_index < hunk.file2_start ||
        edit.line2_index >= hunk.file2_end) {
      continue;
    }
    while (i2 < edit.line2_index && i2 < hunk.file2_end) {
      dput("  ");
      dput(lines2[i2]);
      dput("\n");
      ++i2;
    }
    dput(mixed_change ? "! " : "+ ");
    dput(lines2[edit.line2_index]);
    dput("\n");
    ++i2;
  }
  while (i2 < hunk.file2_end) {
    dput("  ");
    dput(lines2[i2]);
    dput("\n");
    ++i2;
  }
}

auto output_context_diff(const std::string &path1, const std::string &path2,
                         const std::vector<std::string> &compare_lines1,
                         const std::vector<std::string> &compare_lines2,
                         const std::vector<std::string> &lines1,
                         const std::vector<std::string> &lines2, int context,
                         const DiffLabel &label1, const DiffLabel &label2,
                         const StyleCtx &style = {})
    -> void {
  auto edits = compute_diff(compare_lines1, compare_lines2);
  auto hunks = build_diff_hunks(edits, lines1.size(), lines2.size(), context);
  if (hunks.empty()) return;

  output_diff_file_header("*** ", label1, path1, style);
  output_diff_file_header("--- ", label2, path2, style);

  const DiffColors &colors = style.colors ? *style.colors : DiffColors{};
  auto hunk_function = [&](const DiffHunk &hunk) -> std::string {
    if (!style.func_re && !style.func_show_c) return {};
    return find_function_line(lines1, hunk.file1_start, style.func_re,
                              style.func_show_c);
  };

  for (const auto &hunk : hunks) {
    bool has_delete = false;
    bool has_insert = false;
    for (size_t i = hunk.edit_start; i < hunk.edit_end; ++i) {
      has_delete = has_delete || edits[i].type == EditType::DEL;
      has_insert = has_insert || edits[i].type == EditType::INS;
    }
    const bool mixed_change = has_delete && has_insert;

    dput(colors.bold("***************"));
    dput("\n");
    std::string old_header =
        "*** " + format_context_range(hunk.file1_start + 1,
                                      hunk.file1_end - hunk.file1_start) +
        " ****";
    std::string func = hunk_function(hunk);
    if (!func.empty()) old_header += " " + func;
    dput(colors.cyan(old_header));
    dput("\n");
    output_context_old_section(edits, lines1, hunk, mixed_change, colors);
    std::string new_header =
        "--- " + format_context_range(hunk.file2_start + 1,
                                      hunk.file2_end - hunk.file2_start) +
        " ----";
    dput(colors.cyan(new_header));
    dput("\n");
    output_context_new_section(edits, lines2, hunk, mixed_change, colors);
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
                         const std::vector<std::string> &lines2, int width,
                         bool suppress_common, bool left_column, int tabsize,
                         bool expand_tabs)
    -> void {
  (void)path1;
  (void)path2;
  auto edits = compute_diff(compare_lines1, compare_lines2);

  if (edits.empty()) {
    return;  // Files are identical
  }

  // [GNU diffutils side.c] Column layout: the gutter is at least 3 wide
  // and column2_offset is an integral number of tab stops so the right
  // column lines up across rows.
  const intmax_t t = expand_tabs ? 1 : (tabsize > 0 ? tabsize : 8);
  const intmax_t content_t = tabsize > 0 ? tabsize : 8;
  const intmax_t w = width;
  const intmax_t t_plus_g = t + 3;
  const intmax_t unaligned_off =
      (w >> 1) + (t_plus_g >> 1) + (w & t_plus_g & 1);
  const intmax_t off = unaligned_off - unaligned_off % t;
  const intmax_t half_width = off > 0 ? std::max<intmax_t>(0, std::min(off - 3, w - off)) : 0;
  const intmax_t c2o = half_width ? off : w;
  const intmax_t sep_col = (half_width + c2o - 1) >> 1;

  // Emits the line bounded to out_bound print columns, preserving tabs,
  // and returns the output column after it (GNU print_half_line).
  auto print_half_line = [&](const std::string &line, intmax_t out_bound) {
    intmax_t in_pos = 0;
    intmax_t out_pos = 0;
    for (char ch : line) {
      if (ch == '\n') break;
      if (ch == '\t') {
        in_pos = (in_pos / content_t + 1) * content_t;
        if (in_pos <= out_bound) {
          if (expand_tabs) {
            while (out_pos < in_pos) dput(" ");
          } else {
            dput("\t");
          }
          out_pos = in_pos;
          continue;
        }
        break;
      }
      ++in_pos;
      if (in_pos <= out_bound) {
        dput(std::string(1, ch));
        out_pos = in_pos;
      } else {
        break;
      }
    }
    return out_pos;
  };
  auto tab_from_to = [&](intmax_t from, intmax_t to) {
    if (t > 1) {
      for (intmax_t tab = from + t - from % t; tab <= to; tab += t) {
        dput("\t");
        from = tab;
      }
    }
    while (from++ < to) dput(" ");
    return from - 1;
  };

  // [GNU] Consecutive changed rows are aligned: min(deletes, inserts)
  // pairs print '|', extra deletes '<', extra inserts '>'.
  size_t k = 0;
  while (k < edits.size()) {
    if (edits[k].type == EditType::KEEP) {
      auto emit_common = [&](size_t a, size_t b) {
        intmax_t col = print_half_line(lines1[a], half_width);
        if (left_column) {
          // [GNU] --left-column marks the suppressed right column.
          tab_from_to(col, sep_col);
          dput("(");
          dput("\n");
          return;
        }
        col = tab_from_to(col, c2o);
        print_half_line(lines2[b], w);
        dput("\n");
      };
      while (k < edits.size() && edits[k].type == EditType::KEEP) {
        if (!suppress_common) emit_common(edits[k].line1_index, edits[k].line2_index);
        ++k;
      }
      continue;
    }
    std::vector<size_t> dels;
    std::vector<size_t> ins;
    while (k < edits.size() && edits[k].type != EditType::KEEP) {
      if (edits[k].type == EditType::DEL) {
        dels.push_back(edits[k].line1_index);
      } else {
        ins.push_back(edits[k].line2_index);
      }
      ++k;
    }
    const size_t paired = std::min(dels.size(), ins.size());
    for (size_t row = 0; row < paired; ++row) {
      intmax_t col = print_half_line(lines1[dels[row]], half_width);
      col = tab_from_to(col, sep_col) + 1;
      dput("|");
      col = tab_from_to(col, c2o);
      print_half_line(lines2[ins[row]], w);
      dput("\n");
    }
    for (size_t row = paired; row < dels.size(); ++row) {
      intmax_t col = print_half_line(lines1[dels[row]], half_width);
      tab_from_to(col, sep_col);
      dput("<\n");
    }
    for (size_t row = paired; row < ins.size(); ++row) {
      intmax_t col = tab_from_to(0, sep_col) + 1;
      dput(">");
      tab_from_to(col, c2o);
      print_half_line(lines2[ins[row]], w);
      dput("\n");
    }
  }
}

// ===== GNU diffutils 3.10 output engines: ed / RCS / format directives =====


// Returns the function-header line GNU prints after a hunk header: the
// most recent line at or above `bound` (0-based, exclusive) matching the
// -F pattern, or the -p default (leading alphabetic char / _ / $).
auto find_function_line(const std::vector<std::string> &lines, size_t bound,
                        const portable_regex::Pattern *func_re,
                        bool func_show_c) -> std::string {
  auto matches = [&](const std::string &line) {
    if (func_re) {
      return !func_re->find_all(line).empty();
    }
    if (func_show_c && !line.empty()) {
      unsigned char c = static_cast<unsigned char>(line[0]);
      if (std::isalpha(c) || line[0] == '_' || line[0] == '$') return true;
    }
    return false;
  };
  size_t i = std::min(bound, lines.size());
  while (i-- > 0) {
    if (matches(lines[i])) {
      std::string text = lines[i];
      // GNU truncates long function lines instead of wrapping headers.
      if (text.size() > 40) text.resize(40);
      while (!text.empty() &&
             (text.back() == ' ' || text.back() == '\r')) {
        text.pop_back();
      }
      return text;
    }
  }
  return {};
}

// --- ed / forward-ed / RCS script printers -------------------------------
//
// Numbering (validated against GNU diffutils 3.10):
//   ed (-e): hunks in REVERSE order; "A,Bc" comma ranges, "Na"/"Nd" for
//            single-line append/delete; new text then "."; deletes have
//            no body and no dot.
//   forward-ed (-f): forward order, space-separated ranges ("c8 9").
//   RCS (-n): forward order; "dSTART COUNT" then "aANCHOR COUNT" where
//            ANCHOR is the old-file line after which the new text goes
//            (hunk's last deleted line for changes, the line before the
//            insertion for pure inserts, 0 when prepending).

struct ScriptHunk {
  size_t old_start = 0;  // 1-based, inclusive
  size_t old_count = 0;
  size_t new_start = 0;  // 1-based, inclusive
  size_t new_count = 0;
  size_t ins_anchor = 0;  // old-file line the insertion follows (0 = prepend)
  std::vector<size_t> del_idx;  // indices into lines1
  std::vector<size_t> ins_idx;  // indices into lines2
};

auto build_script_hunks(const std::vector<Edit> &edits) -> std::vector<ScriptHunk> {
  std::vector<ScriptHunk> hunks;
  bool in_hunk = false;
  for (const auto &edit : edits) {
    if (edit.type == EditType::KEEP) {
      in_hunk = false;
      continue;
    }
    if (!in_hunk) {
      hunks.emplace_back();
      in_hunk = true;
    }
    auto &hunk = hunks.back();
    if (edit.type == EditType::DEL) {
      if (hunk.del_idx.empty()) hunk.old_start = edit.line1_index + 1;
      hunk.old_count++;
      hunk.del_idx.push_back(edit.line1_index);
    } else {
      if (hunk.ins_idx.empty()) {
        hunk.new_start = edit.line2_index + 1;
        hunk.ins_anchor = edit.line1_index;
      }
      hunk.new_count++;
      hunk.ins_idx.push_back(edit.line2_index);
    }
  }
  return hunks;
}

auto format_ed_range(size_t start, size_t count) -> std::string {
  return count > 1 ? std::to_string(start) + "," + std::to_string(start + count - 1)
                   : std::to_string(start);
}

auto format_fwd_range(size_t start, size_t count) -> std::string {
  return count > 1 ? std::to_string(start) + " " + std::to_string(start + count - 1)
                   : std::to_string(start);
}

template <typename Lines>
void output_ed_script(const std::vector<Edit> &edits, const Lines &lines1,
                      const Lines &lines2, bool forward) {
  (void)lines1;
  auto hunks = build_script_hunks(edits);
  if (forward) {
    for (const auto &hunk : hunks) {
      // Forward-ed puts the command letter first: "c2", "a1", "d8 9".
      if (hunk.old_count > 0 && hunk.new_count > 0) {
        dput("c" + format_fwd_range(hunk.old_start, hunk.old_count) + "\n");
        for (size_t idx : hunk.ins_idx) dput(lines2[idx] + "\n");
        dput(".\n");
      } else if (hunk.new_count > 0) {
        dput("a" + format_fwd_range(hunk.ins_anchor, 1) + "\n");
        for (size_t idx : hunk.ins_idx) dput(lines2[idx] + "\n");
        dput(".\n");
      } else {
        dput("d" + format_fwd_range(hunk.old_start, hunk.old_count) + "\n");
      }
    }
  } else {
    for (auto hunk = hunks.rbegin(); hunk != hunks.rend(); ++hunk) {
      if (hunk->old_count > 0 && hunk->new_count > 0) {
        dput(format_ed_range(hunk->old_start, hunk->old_count) + "c\n");
        for (size_t idx : hunk->ins_idx) dput(lines2[idx] + "\n");
        dput(".\n");
      } else if (hunk->new_count > 0) {
        dput(std::to_string(hunk->ins_anchor) + "a\n");
        for (size_t idx : hunk->ins_idx) dput(lines2[idx] + "\n");
        dput(".\n");
      } else {
        dput(format_ed_range(hunk->old_start, hunk->old_count) + "d\n");
      }
    }
  }
}

auto format_rcs_count(size_t start, size_t count) -> std::string {
  return std::to_string(start) + " " + std::to_string(count);
}

template <typename Lines>
void output_rcs_diff(const std::vector<Edit> &edits, const Lines &lines1,
                     const Lines &lines2) {
  (void)lines1;
  auto hunks = build_script_hunks(edits);
  for (const auto &hunk : hunks) {
    if (hunk.old_count > 0) {
      dput("d" + format_rcs_count(hunk.old_start, hunk.old_count) + "\n");
    }
    if (hunk.new_count > 0) {
      // Anchor: last old line of the change, else the common line above
      // the insertion (old_start-1), 0 when prepending.
      size_t anchor = hunk.old_count > 0 ? hunk.old_start + hunk.old_count - 1
                                         : (hunk.old_start > 0 ? hunk.old_start - 1 : 0);
      dput("a" + format_rcs_count(anchor, hunk.new_count) + "\n");
      for (size_t idx : hunk.ins_idx) dput(lines2[idx] + "\n");
    }
  }
}

// --- --ifdef / --*-group-format / --*-line-format directives ------------
//
// GFMT/LFMT expansion per GNU diffutils "Customizing Output":
//   %% literal %            %< old lines   %> new lines   %= common lines
//   %[-][WIDTH][.[PREC]]{doxX}LETTER  F first  L last  N count  E F-1  M L+1
//   (uppercase = new file in changed groups, lowercase = old)
//   %(A=B?T:E) conditional;  %c'C' and %c'\OOO' character literals
//   LFMT-only: %L line with newline, %l without, %n line number

struct FormatDirectives {
  std::string changed_group, old_group, new_group, unchanged_group;
  std::string line, old_line, new_line, unchanged_line;
};

auto directive_has_any(const FormatDirectives &f) -> bool {
  return !f.changed_group.empty() || !f.old_group.empty() ||
         !f.new_group.empty() || !f.unchanged_group.empty() ||
         !f.line.empty() || !f.old_line.empty() || !f.new_line.empty() ||
         !f.unchanged_line.empty();
}

struct GroupInfo {
  size_t old_start = 0;  // 1-based
  size_t old_count = 0;
  size_t new_start = 0;  // 1-based
  size_t new_count = 0;
};

// printf-style number expansion for %[-][WIDTH][.[PREC]]{doxX}LETTER
auto expand_number_directive(std::string_view spec, char letter, size_t value,
                             const GroupInfo &group) -> std::string {
  // Uppercase letters address the new file, lowercase the old file.
  const bool old_file = std::islower(static_cast<unsigned char>(letter)) != 0;
  const size_t first = old_file ? group.old_start : group.new_start;
  const size_t count = old_file ? group.old_count : group.new_count;
  unsigned long long number = value;
  switch (std::toupper(static_cast<unsigned char>(letter))) {
    case 'F': number = first; break;
    case 'L': number = count ? first + count - 1 : 0; break;
    case 'N': number = count; break;
    case 'E': number = first ? first - 1 : 0; break;
    case 'M': number = first + count; break;
    default: break;
  }

  // Parse [[-]WIDTH][.PREC] then conversion char.
  size_t i = 0;
  bool left = false;
  if (i < spec.size() && spec[i] == '-') {
    left = true;
    ++i;
  }
  size_t width = 0;
  bool has_width = false;
  while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
    width = width * 10 + static_cast<size_t>(spec[i] - '0');
    has_width = true;
    ++i;
  }
  size_t prec = 0;
  if (i < spec.size() && spec[i] == '.') {
    ++i;
    while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
      prec = prec * 10 + static_cast<size_t>(spec[i] - '0');
      ++i;
    }
  }
  char conv = i < spec.size() ? spec[i] : 'd';
  char buf[64]{};
  switch (conv) {
    case 'o': std::snprintf(buf, sizeof(buf), "%llo", number); break;
    case 'x': std::snprintf(buf, sizeof(buf), "%llx", number); break;
    case 'X': std::snprintf(buf, sizeof(buf), "%llX", number); break;
    default:  std::snprintf(buf, sizeof(buf), "%llu", number); break;
  }
  std::string digits(buf);
  if (digits.size() < prec) digits.insert(0, prec - digits.size(), '0');
  if (digits.size() < width && !left) {
    digits.insert(0, width - digits.size(), ' ');
  } else if (digits.size() < width && left) {
    digits.append(width - digits.size(), ' ');
  }
  return digits;
}

auto expand_format(const std::string &fmt, const std::vector<std::string> &lines1,
                   const std::vector<std::string> &lines2, const GroupInfo &group,
                   bool group_is_changed, const std::string *single_line,
                   size_t single_line_no, bool single_from_old) -> std::string;

auto expand_conditional(const std::string &fmt, size_t &i,
                        const std::vector<std::string> &lines1,
                        const std::vector<std::string> &lines2,
                        const GroupInfo &group, bool group_is_changed,
                        const std::string *single_line, size_t single_line_no,
                        bool single_from_old) -> std::string {
  // fmt[i] == '(' after '%('. Grammar: A=B?T:E with A,B letters.
  auto letter_value = [&](char letter) -> long long {
    switch (letter) {
      case 'F': return static_cast<long long>(group.new_start);
      case 'f': return static_cast<long long>(group.old_start);
      case 'L': return static_cast<long long>(group.new_count ? group.new_start + group.new_count - 1 : 0);
      case 'l': return static_cast<long long>(group.old_count ? group.old_start + group.old_count - 1 : 0);
      case 'N': return static_cast<long long>(group.new_count);
      case 'n': return static_cast<long long>(group.old_count);
      case 'E': return static_cast<long long>(group.new_start ? group.new_start - 1 : 0);
      case 'e': return static_cast<long long>(group.old_start ? group.old_start - 1 : 0);
      case 'M': return static_cast<long long>(group.new_count ? group.new_start + group.new_count : 0);
      case 'm': return static_cast<long long>(group.old_count ? group.old_start + group.old_count : 0);
      default: return 0;
    }
  };
  size_t p = i + 1;
  char a = p < fmt.size() ? fmt[p] : '=';
  ++p;
  bool eq = p < fmt.size() && fmt[p] == '=';
  if (!eq) return "%(";
  ++p;
  char b = p < fmt.size() ? fmt[p] : '=';
  ++p;
  if (p >= fmt.size() || fmt[p] != '?') return "%(";
  ++p;
  size_t t_start = p;
  int depth = 0;
  while (p < fmt.size() && (depth > 0 || (fmt[p] != ':' && fmt[p] != ')'))) {
    if (fmt[p] == '(') ++depth;
    if (fmt[p] == ')') --depth;
    ++p;
  }
  std::string t_branch = fmt.substr(t_start, p - t_start);
  std::string e_branch;
  if (p < fmt.size() && fmt[p] == ':') {
    ++p;
    size_t e_start = p;
    depth = 0;
    while (p < fmt.size() && (depth > 0 || fmt[p] != ')')) {
      if (fmt[p] == '(') ++depth;
      if (fmt[p] == ')') --depth;
      ++p;
    }
    e_branch = fmt.substr(e_start, p - e_start);
  }
  if (p < fmt.size() && fmt[p] == ')') ++p;
  bool condition = letter_value(a) == letter_value(b);
  const std::string &chosen = condition ? t_branch : e_branch;
  i = p;
  return expand_format(chosen, lines1, lines2, group, group_is_changed,
                       single_line, single_line_no, single_from_old);
}

auto expand_format(const std::string &fmt, const std::vector<std::string> &lines1,
                   const std::vector<std::string> &lines2, const GroupInfo &group,
                   bool group_is_changed, const std::string *single_line,
                   size_t single_line_no, bool single_from_old) -> std::string {
  std::string out;
  size_t i = 0;
  while (i < fmt.size()) {
    if (fmt[i] != '%') {
      out += fmt[i++];
      continue;
    }
    ++i;
    if (i >= fmt.size()) {
      out += '%';
      break;
    }
    char c = fmt[i];
    if (c == '%') {
      out += '%';
      ++i;
    } else if (c == '<') {
      if (single_line) {
        out += single_from_old ? *single_line : std::string();
      } else {
        for (size_t k = 0; k < group.old_count; ++k) {
          out += lines1[group.old_start - 1 + k];
          out += "\n";
        }
      }
      ++i;
    } else if (c == '>') {
      if (single_line) {
        out += single_from_old ? std::string() : *single_line;
      } else {
        for (size_t k = 0; k < group.new_count; ++k) {
          out += lines2[group.new_start - 1 + k];
          out += "\n";
        }
      }
      ++i;
    } else if (c == '=') {
      if (single_line) {
        out += *single_line;
      } else {
        for (size_t k = 0; k < group.old_count; ++k) {
          out += lines1[group.old_start - 1 + k];
          out += "\n";
        }
      }
      ++i;
    } else if (c == 'L') {
      if (single_line) out += *single_line;
      ++i;
    } else if (c == 'l') {
      if (single_line) {
        std::string_view v = *single_line;
        if (!v.empty() && v.back() == '\n') v.remove_suffix(1);
        out += std::string(v);
      }
      ++i;
    } else if (c == '(') {
      out += expand_conditional(fmt, i, lines1, lines2, group,
                                group_is_changed, single_line,
                                single_line_no, single_from_old);
    } else if (c == 'c') {
      // %c'C' or %c'\OOO'
      if (i + 2 < fmt.size() && fmt[i + 1] == '\'') {
        if (fmt[i + 2] == '\\' && i + 5 < fmt.size() && fmt[i + 5] == '\'') {
          int code = 0;
          for (int k = i + 3; k < i + 5; ++k) {
            code = code * 8 + (fmt[k] - '0');
          }
          out += static_cast<char>(code);
          i += 6;
        } else {
          out += fmt[i + 2];
          i += 4;
        }
      } else {
        out += '%';
        ++i;
      }
    } else {
      // Number directive: optional -/width/prec then conversion then letter.
      size_t j = i;
      if (j < fmt.size() && fmt[j] == '-') ++j;
      while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') ++j;
      if (j < fmt.size() && fmt[j] == '.') {
        ++j;
        while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') ++j;
      }
      char conv = (j < fmt.size() && (fmt[j] == 'd' || fmt[j] == 'o' ||
                                      fmt[j] == 'x' || fmt[j] == 'X'))
                      ? fmt[j]
                      : 'd';
      if (conv != 'd') ++j;
      if (j < fmt.size()) {
        char letter = fmt[j];
        size_t used = j - i;
        std::string spec = fmt.substr(i, used);
        if (letter == 'n' && single_line) {
          // Line-context %n: the line's own number.
          out += expand_number_directive(spec, 'n', single_line_no, group);
        } else if (std::strchr("FLNEMflnem", letter)) {
          out += expand_number_directive(spec, letter, 0, group);
        } else {
          out += fmt.substr(i - 1, used + 2);
        }
        i = j + 1;
      } else {
        out += '%';
        ++i;
      }
    }
  }
  return out;
}

auto default_ifdef_formats(const std::string &name) -> FormatDirectives {
  FormatDirectives f;
  f.changed_group = "#ifndef " + name + "\n%<#else /* " + name +
                    " */\n%>#endif /* " + name + " */\n";
  f.old_group = "#ifndef " + name + "\n%<#endif /* ! " + name + " */\n";
  f.new_group = "#ifdef " + name + "\n%>#endif /* " + name + " */\n";
  f.unchanged_group = "%=";
  f.unchanged_line = "%L";
  f.old_line = "%L";
  f.new_line = "%L";
  return f;
}

// Walks the edit script emitting custom/ifdef formatted output: maximal
// runs of non-KEEP edits are groups (changed when both sides present);
// common runs emit per-line or the unchanged-group format.
void output_format_diff(const std::vector<Edit> &edits,
                        const std::vector<std::string> &lines1,
                        const std::vector<std::string> &lines2,
                        const FormatDirectives &formats) {
  size_t i = 0;
  while (i < edits.size()) {
    if (edits[i].type == EditType::KEEP) {
      GroupInfo group;
      group.old_start = edits[i].line1_index + 1;
      group.new_start = edits[i].line2_index + 1;
      // Consume the whole common run; groups get one expansion, plain
      // line formats get one per line.
      size_t j = i;
      while (j < edits.size() && edits[j].type == EditType::KEEP) ++j;
      group.old_count = j - i;
      group.new_count = j - i;
      if (!formats.unchanged_group.empty()) {
        GroupInfo as_group = group;
        as_group.new_count = group.old_count;
        dput(expand_format(formats.unchanged_group, lines1, lines2, as_group,
                           false, nullptr, 0, false));
      } else {
        const std::string &lf = !formats.unchanged_line.empty()
                                    ? formats.unchanged_line
                                    : formats.line;
        for (size_t k = i; k < j; ++k) {
          std::string line = lines1[edits[k].line1_index];
          if (line.empty() || line.back() != '\n') line += "\n";
          if (lf.empty()) {
            dput(line);
          } else {
            GroupInfo one;
            one.old_start = one.new_start = edits[k].line1_index + 1;
            dput(expand_format(lf, lines1, lines2, one, false, &line,
                               edits[k].line1_index + 1, true));
          }
        }
      }
      i = j;
      continue;
    }
    size_t j = i;
    GroupInfo group;
    while (j < edits.size() && edits[j].type != EditType::KEEP) {
      if (edits[j].type == EditType::DEL) {
        if (group.old_count == 0) group.old_start = edits[j].line1_index + 1;
        ++group.old_count;
      } else {
        if (group.new_count == 0) group.new_start = edits[j].line2_index + 1;
        ++group.new_count;
      }
      ++j;
    }
    const std::string *chosen = nullptr;
    if (group.old_count > 0 && group.new_count > 0) {
      chosen = &formats.changed_group;
    } else if (group.old_count > 0) {
      chosen = &formats.old_group;
    } else {
      chosen = &formats.new_group;
    }
    if (chosen && !chosen->empty()) {
      dput(expand_format(*chosen, lines1, lines2, group, true, nullptr, 0,
                         false));
    } else {
      // No group format for this shape: fall back to per-line formats.
      const std::string &old_lf = !formats.old_line.empty() ? formats.old_line
                                                            : formats.line;
      const std::string &new_lf = !formats.new_line.empty() ? formats.new_line
                                                            : formats.line;
      for (size_t k = i; k < j; ++k) {
        bool from_old = edits[k].type == EditType::DEL;
        std::string line = from_old ? lines1[edits[k].line1_index]
                                    : lines2[edits[k].line2_index];
        if (line.empty() || line.back() != '\n') line += "\n";
        if (old_lf.empty() && new_lf.empty()) {
          dput(line);
        } else {
          const std::string &lf = from_old ? old_lf : new_lf;
          GroupInfo one;
          one.old_start = edits[k].line1_index + 1;
          one.new_start = edits[k].line2_index + 1;
          dput(expand_format(lf, lines1, lines2, one, true, &line,
                             from_old ? edits[k].line1_index + 1
                                      : edits[k].line2_index + 1,
                             from_old));
        }
      }
    }
    i = j;
  }
}


// ---- Per-pair configuration shared by file and directory modes ----
enum class OutputMode {
  Normal,
  Unified,
  Context,
  SideBySide,
  Ed,
  ForwardEd,
  Rcs,
  Format
};

// ---- Per-pair configuration shared by file and directory modes ----
struct PairConfig {
  bool brief = false;
  bool ignore_all_space = false;
  bool ignore_blank_lines = false;
  bool ignore_space_change = false;
  bool ignore_case = false;
  bool ignore_trailing_space = false;
  bool ignore_tab_expansion = false;
  bool strip_trailing_cr = false;
  bool text_mode = false;
  bool binary_mode = false;
  bool report_identical = false;
  bool new_file = false;
  bool unidirectional_new_file = false;
  int tabsize = 8;
  bool expand_tabs = false;
  std::string ignore_matching;
  OutputMode output_mode = OutputMode::Normal;
  int context = 3;
  int side_width = 130;
  bool suppress_common = false;
  bool left_column = false;
  StyleCtx style;
  FormatDirectives formats;
  std::string diff_program;
};

// fnmatch-style * / ? glob used for -x/-X/--exclude-dir name filtering.
auto glob_match_pattern(std::string_view name, std::string_view pattern)
    -> bool {
  size_t n = 0;
  size_t p = 0;
  size_t star_p = std::string_view::npos;
  size_t star_n = 0;
  while (n < name.size()) {
    if (p < pattern.size() &&
        (pattern[p] == '?' || pattern[p] == name[n])) {
      ++n;
      ++p;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star_p = p++;
      star_n = n;
    } else if (star_p != std::string_view::npos) {
      p = star_p + 1;
      n = ++star_n;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

auto compare_file_pair(const std::string &file1, const std::string &file2,
                       DiffLabel label1, DiffLabel label2,
                       const PairConfig &cfg) -> int {
  // [GNU] Missing operands are reported under their original names; -N
  // (and --unidirectional-new-file for the first operand) treats them as
  // empty instead.
  for (const std::string *operand_path : {&file1, &file2}) {
    if (cfg.new_file || operand_is_stdin(*operand_path)) continue;
    if (!cfg.unidirectional_new_file || operand_path != &file1) {
      auto operand = native_path::make_api_path_operand(*operand_path);
      if (native_path::operand_target_attributes_w(operand) ==
          INVALID_FILE_ATTRIBUTES) {
        safeErrorPrint("diff: ");
        safeErrorPrint(*operand_path);
        safeErrorPrint(": No such file or directory\n");
        return 2;
      }
    }
  }

  namespace fs = std::filesystem;
  auto lines1_result = read_file_lines_result(file1);
  auto lines2_result = read_file_lines_result(file2);
  bool input_error = false;
  if (!lines1_result) {
    // -N and --unidirectional-new-file (first operand only) treat an
    // unreadable/absent side as empty.
    if (cfg.new_file || cfg.unidirectional_new_file) {
      lines1_result = std::vector<std::string>{};
    } else {
      safeErrorPrint("diff: ");
      safeErrorPrint(lines1_result.error());
      safeErrorPrint("\n");
      input_error = true;
    }
  }
  if (!lines2_result) {
    if (cfg.new_file) {
      lines2_result = std::vector<std::string>{};
    } else {
      safeErrorPrint("diff: ");
      safeErrorPrint(lines2_result.error());
      safeErrorPrint("\n");
      input_error = true;
    }
  }
  if (input_error) {
    return 2;
  }

  auto &lines1 = lines1_result.value();
  auto &lines2 = lines2_result.value();

  // [GNU] --diff-program=PROGRAM: hand the two operands to PROGRAM and
  // relay its output and exit status verbatim.
  if (!cfg.diff_program.empty() && !operand_is_stdin(file1) &&
      !operand_is_stdin(file2)) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string cmdline = "\"" + cfg.diff_program + "\" \"" + file1 +
                          "\" \"" + file2 + "\"";
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &si, &pi)) {
      safeErrorPrint("diff: cannot execute ");
      safeErrorPrint(cfg.diff_program);
      safeErrorPrint("\n");
      return 2;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code > 1 ? 2 : static_cast<int>(code);
  }

  if (cfg.brief) {
    return compare_files(file1, file2, lines1, lines2, true,
                         cfg.ignore_all_space)
               ? 0
               : 1;
  }

  // -- Apply transformations for comparison [DIFFERS] --
  auto cmp1 = lines1;
  auto cmp2 = lines2;

  if (cfg.strip_trailing_cr) {
    cmp1 = strip_trailing_cr_lines(cmp1);
    cmp2 = strip_trailing_cr_lines(cmp2);
  }
  if (cfg.ignore_trailing_space) {
    auto strip_trailing_ws = [](const std::vector<std::string> &lines) {
      std::vector<std::string> out;
      out.reserve(lines.size());
      for (const auto &line : lines) {
        size_t end = line.size();
        while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t' ||
                           line[end - 1] == '\r')) {
          --end;
        }
        out.push_back(line.substr(0, end));
      }
      return out;
    };
    cmp1 = strip_trailing_ws(cmp1);
    cmp2 = strip_trailing_ws(cmp2);
  }
  if (cfg.ignore_tab_expansion) {
    // [GNU] Trailing whitespace caused by tab expansion is ignored: expand
    // tabs only in the trailing whitespace run, then drop it.
    auto expand_trailing = [ts = cfg.tabsize](const std::vector<std::string> &lines) {
      std::vector<std::string> out;
      out.reserve(lines.size());
      for (const auto &line : lines) {
        size_t end = line.size();
        while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
          --end;
        }
        std::string tail;
        int col = 0;
        for (size_t i = end; i < line.size(); ++i) {
          if (line[i] == '\t') {
            int spaces = ts - (col % ts);
            tail.append(static_cast<size_t>(spaces), ' ');
            col += spaces;
          } else {
            tail += ' ';
            ++col;
          }
        }
        out.push_back(line.substr(0, end) + tail);
      }
      return out;
    };
    cmp1 = expand_trailing(cmp1);
    cmp2 = expand_trailing(cmp2);
  }
  if (cfg.ignore_blank_lines) {
    cmp1 = filter_blank_lines(cmp1);
    cmp2 = filter_blank_lines(cmp2);
  }
  if (cfg.expand_tabs) {
    cmp1 = expand_tabs_lines(cmp1, cfg.tabsize);
    cmp2 = expand_tabs_lines(cmp2, cfg.tabsize);
  }
  cmp1 = normalize_lines_for_compare(cmp1, cfg.ignore_all_space);
  cmp2 = normalize_lines_for_compare(cmp2, cfg.ignore_all_space);
  if (cfg.ignore_space_change) {
    cmp1 = normalize_lines_space_change(cmp1);
    cmp2 = normalize_lines_space_change(cmp2);
  }
  if (cfg.ignore_case) {
    cmp1 = normalize_lines_case(cmp1);
    cmp2 = normalize_lines_case(cmp2);
  }
  if (!cfg.ignore_matching.empty()) {
    cmp1 = filter_matching_lines(cmp1, cfg.ignore_matching);
    cmp2 = filter_matching_lines(cmp2, cfg.ignore_matching);
  }
  auto &compare_lines1 = cmp1;
  auto &compare_lines2 = cmp2;

  if (compare_lines1 == compare_lines2) {
    if (cfg.report_identical) {
      dput("Files ");
      dput(file1);
      dput(" and ");
      dput(file2);
      dput(" are identical\n");
    }
    return 0;
  }

  switch (cfg.output_mode) {
    case OutputMode::Unified:
      output_unified_diff(file1, file2, compare_lines1, compare_lines2,
                          lines1, lines2, cfg.context, label1, label2,
                          cfg.style);
      break;
    case OutputMode::Context:
      output_context_diff(file1, file2, compare_lines1, compare_lines2,
                          lines1, lines2, cfg.context, label1, label2,
                          cfg.style);
      break;
    case OutputMode::SideBySide:
      output_side_by_side(file1, file2, compare_lines1, compare_lines2,
                          lines1, lines2, cfg.side_width, cfg.suppress_common,
                          cfg.left_column, cfg.tabsize, cfg.expand_tabs);
      break;
    case OutputMode::Ed:
      output_ed_script(compute_diff(compare_lines1, compare_lines2), lines1,
                       lines2, false);
      break;
    case OutputMode::ForwardEd:
      output_ed_script(compute_diff(compare_lines1, compare_lines2), lines1,
                       lines2, true);
      break;
    case OutputMode::Rcs:
      output_rcs_diff(compute_diff(compare_lines1, compare_lines2), lines1,
                      lines2);
      break;
    case OutputMode::Format:
      output_format_diff(compute_diff(compare_lines1, compare_lines2), lines1,
                         lines2, cfg.formats);
      break;
    case OutputMode::Normal:
    default: {
      // GNU normal format: per-hunk command line (Na / Nd / NcN) with the
      // deleted lines (<), a `---` separator for changes, and added (>).
      auto edits = compute_diff(compare_lines1, compare_lines2);
      auto hunks =
          build_diff_hunks(edits, lines1.size(), lines2.size(), 0);
      if (hunks.empty()) {
        return 0;
      }
      std::sort(hunks.begin(), hunks.end(),
                [](const auto &a, const auto &b) {
                  if (a.file1_start != b.file1_start) {
                    return a.file1_start < b.file1_start;
                  }
                  return a.file2_start < b.file2_start;
                });
      auto format_normal_range = [](size_t start_line, size_t count) {
        std::string out = std::to_string(start_line);
        if (count > 1) {
          out += ",";
          out += std::to_string(start_line + count - 1);
        }
        return out;
      };
      for (const auto &hunk : hunks) {
        std::vector<size_t> del_lines;
        std::vector<size_t> ins_lines;
        for (size_t i = hunk.edit_start; i < hunk.edit_end; ++i) {
          const auto &edit = edits[i];
          if (edit.type == EditType::DEL) {
            del_lines.push_back(edit.line1_index);
          } else if (edit.type == EditType::INS) {
            ins_lines.push_back(edit.line2_index);
          }
        }
        std::sort(del_lines.begin(), del_lines.end());
        std::sort(ins_lines.begin(), ins_lines.end());
        if (del_lines.empty() && ins_lines.empty()) continue;
        const size_t n_del = hunk.file1_end - hunk.file1_start;
        const size_t n_ins = hunk.file2_end - hunk.file2_start;
        if (n_del == 0) {
          dput(std::to_string(hunk.file1_start));
          dput("a");
          dput(format_normal_range(hunk.file2_start + 1, n_ins));
          dput("\n");
        } else if (n_ins == 0) {
          dput(format_normal_range(hunk.file1_start + 1, n_del));
          dput("d");
          dput(std::to_string(hunk.file2_start));
          dput("\n");
        } else {
          dput(format_normal_range(hunk.file1_start + 1, n_del));
          dput("c");
          dput(format_normal_range(hunk.file2_start + 1, n_ins));
          dput("\n");
        }
        for (size_t idx : del_lines) {
          dput("< ");
          dput(lines1[idx]);
          dput("\n");
        }
        if (!del_lines.empty() && !ins_lines.empty()) {
          dput("---\n");
        }
        for (size_t idx : ins_lines) {
          dput("> ");
          dput(lines2[idx]);
          dput("\n");
        }
      }
      break;
    }
  }

  return 1;
}

// One directory-pair level: compare same-named files (with the "diff
// <cmdline>" header GNU prints under -r), report "Only in", and recurse
// into common subdirectories. diffutils processes files before dirs.
auto compare_directory_pair(const std::string &dir1, const std::string &dir2,
                            const PairConfig &cfg, bool recursive,
                            const std::vector<std::string> &exclude_pats,
                            const std::vector<std::string> &exclude_dir_pats,
                            const std::string &starting_file,
                            const std::string &command_tail) -> int {
  namespace fs = std::filesystem;
  auto list_directory = [](const std::string &dir)
      -> std::optional<std::vector<std::string>> {
    std::error_code ec;
    fs::directory_iterator it(fs::path(dir), ec);
    if (ec) return std::nullopt;
    std::vector<std::string> names;
    for (const auto &entry : it) {
      std::string name = entry.path().filename().string();
      if (name == "." || name == "..") continue;
      names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
  };
  auto name_matches_any = [](const std::string &name,
                             const std::vector<std::string> &patterns) {
    for (const auto &pattern : patterns) {
      if (glob_match_pattern(name, pattern)) return true;
    }
    return false;
  };
  auto operand_is_directory = [](const std::string &operand_path) -> bool {
    if (operand_is_stdin(operand_path)) return false;
    auto operand = native_path::make_api_path_operand(operand_path);
    return native_path::attributes_are_directory(
        native_path::operand_target_attributes_w(operand));
  };
  auto join_dir_member = [](const std::string &dir,
                            const std::string &member) -> std::string {
    if (member.empty()) return dir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
      return dir + "/" + member;
    }
    return dir + member;
  };

  auto entries1 = list_directory(dir1);
  auto entries2 = list_directory(dir2);
  if (!entries1 || !entries2) {
    safeErrorPrint("diff: cannot read directory '");
    safeErrorPrint(entries1 ? dir2 : dir1);
    safeErrorPrint("'\n");
    return 2;
  }
  if (!starting_file.empty()) {
    auto rotate = [&](std::vector<std::string> &names) {
      for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == starting_file) {
          std::rotate(names.begin(), names.begin() + static_cast<long>(i),
                      names.end());
          break;
        }
      }
    };
    rotate(*entries1);
    rotate(*entries2);
  }

  int worst = 0;
  auto excluded = [&](const std::string &name, bool is_dir) {
    if (name_matches_any(name, exclude_pats)) return true;
    return is_dir && name_matches_any(name, exclude_dir_pats);
  };

  for (bool dir_pass : {false, true}) {
    size_t i1 = 0;
    size_t i2 = 0;
    while (i1 < entries1->size() || i2 < entries2->size()) {
      bool take1 =
          i1 < entries1->size() &&
          (i2 >= entries2->size() || (*entries1)[i1] <= (*entries2)[i2]);
      bool take2 =
          i2 < entries2->size() &&
          (i1 >= entries1->size() || (*entries2)[i2] < (*entries1)[i1]);
      const std::string &name1 = take1 ? (*entries1)[i1] : (*entries2)[i2];
      const std::string &name2 = take2 ? (*entries2)[i2] : name1;
      std::string full1 = join_dir_member(dir1, name1);
      std::string full2 = join_dir_member(dir2, name2);
      const bool is_dir1 = take1 && operand_is_directory(full1);
      const bool is_dir2 = take2 && operand_is_directory(full2);
      if (excluded(name1, is_dir1) || excluded(name2, is_dir2)) {
        if (take1) ++i1;
        if (take2) ++i2;
        continue;
      }
      const bool both_dirs = is_dir1 && is_dir2;
      if (dir_pass != both_dirs) {
        if (take1) ++i1;
        if (take2) ++i2;
        continue;
      }
      if (take1 && take2 && name1 == name2) {
        if (both_dirs) {
          if (recursive) {
            int status = compare_directory_pair(
                full1, full2, cfg, recursive, exclude_pats, exclude_dir_pats,
                starting_file, command_tail);
            if (status == 2) return 2;
            worst = std::max(worst, status);
          } else {
            dput("Common subdirectories: " + full1 + " and " + full2 + "\n");
            worst = std::max(worst, 1);
          }
        } else {
          if (recursive) {
            dput("diff " + command_tail + " " + full1 + " " + full2 + "\n");
          }
          int status = compare_file_pair(full1, full2, DiffLabel{full1, false},
                                         DiffLabel{full2, false}, cfg);
          if (status == 2) return 2;
          worst = std::max(worst, status);
        }
      } else {
        // Present in only one directory.
        const std::string &only_dir = take1 ? dir1 : dir2;
        const std::string &only_name = take1 ? name1 : name2;
        const bool treat_as_empty =
            cfg.new_file || (cfg.unidirectional_new_file && !take1);
        if (treat_as_empty && !both_dirs) {
          std::string existing = take1 ? full1 : full2;
          std::string missing = take1 ? full2 : full1;
          if (recursive) {
            dput("diff " + command_tail + " " + existing + " " + missing +
                 "\n");
          }
          PairConfig empty_cfg = cfg;
          empty_cfg.new_file = true;
          int status = compare_file_pair(existing, missing,
                                         DiffLabel{existing, false},
                                         DiffLabel{missing, false}, empty_cfg);
          if (status == 2) return 2;
          worst = std::max(worst, status);
        } else {
          dput("Only in " + only_dir + ": " + only_name + "\n");
          worst = std::max(worst, 1);
        }
      }
      if (take1) ++i1;
      if (take2) ++i2;
    }
  }
  return worst;
}

}  // namespace diff_pipeline

REGISTER_COMMAND(
    diff, "diff", "diff [OPTION]... FILES",
    "Compare files line by line.\n",
    "  diff -u old.txt new.txt    Unified diff with 3 context lines\n"
    "  diff -y f1 f2              Two-column side-by-side comparison\n"
    "  diff -r dir1 dir2          Recursively compare directories",
    "cmp(1), diff3(1), sdiff(1), patch(1)", "caomengxuan666",
    "Copyright © 2026 WinuxCmd", DIFF_OPTIONS) {
  using namespace diff_pipeline;

#ifdef _WIN32
  // [GNU] "-" reads raw stdin bytes; text mode would strip CR and corrupt
  // CRLF comparisons (#1057).
  _setmode(_fileno(stdin), _O_BINARY);
#endif

  PairConfig cfg;
  cfg.brief = ctx.get<bool>("-q", false) || ctx.get<bool>("--brief", false);
  cfg.ignore_all_space =
      ctx.get<bool>("-w", false) || ctx.get<bool>("--ignore-all-space", false);
  cfg.ignore_blank_lines = ctx.has("-B") || ctx.has("--ignore-blank-lines");
  cfg.ignore_space_change = ctx.has("-b") || ctx.has("--ignore-space-change");
  cfg.ignore_case = ctx.has("-i") || ctx.has("--ignore-case");
  cfg.text_mode = ctx.has("-a") || ctx.has("--text");
  cfg.binary_mode = ctx.has("--binary");
  cfg.strip_trailing_cr = ctx.has("--strip-trailing-cr");
  cfg.ignore_trailing_space =
      ctx.has("-Z") || ctx.has("--ignore-trailing-space");
  cfg.ignore_tab_expansion =
      ctx.has("-E") || ctx.has("--ignore-tab-expansion");
  cfg.report_identical = ctx.has("--report-identical-files") || ctx.has("-s");
  cfg.new_file = ctx.has("-N") || ctx.has("--new-file");
  cfg.unidirectional_new_file = ctx.has("--unidirectional-new-file");
  cfg.suppress_common = ctx.has("--suppress-common-lines");
  cfg.left_column = ctx.has("--left-column");
  cfg.expand_tabs = ctx.has("-t") || ctx.has("--expand-tabs");
  // [GNU] -T/--initial-tab and --suppress-blank-empty tweak whitespace in
  // the emitted lines; accepted and applied as no-ops for now. -d and
  // --speed-large-files tune search heuristics this exact-LCS engine does
  // not need; --palette re-skins --color only.
  (void)ctx.has("-T");
  (void)ctx.has("--initial-tab");
  (void)ctx.has("--suppress-blank-empty");
  (void)ctx.has("-d");
  (void)ctx.has("--minimal");
  (void)ctx.has("--speed-large-files");
  (void)ctx.has("--palette");
  (void)ctx.has("--no-dereference");
  (void)ctx.has("--ignore-file-name-case");
  (void)ctx.has("--no-ignore-file-name-case");
  (void)ctx.has("--binary");

  if (ctx.has("--tabsize")) cfg.tabsize = ctx.get<int>("--tabsize", 8);
  if (ctx.has("-W")) cfg.side_width = ctx.get<int>("-W", 130);
  if (ctx.has("--width")) cfg.side_width = ctx.get<int>("--width", 130);
  int horizon_lines = 0;
  if (ctx.has("--horizon-lines")) {
    horizon_lines = ctx.get<int>("--horizon-lines", 0);
  }
  (void)horizon_lines;

  cfg.ignore_matching =
      ctx.get<std::string>("--ignore-matching-lines", "");
  std::string ifdef_name =
      ctx.get<std::string>("-D", ctx.get<std::string>("--ifdef", ""));
  std::string from_file = ctx.get<std::string>("--from-file", "");
  std::string to_file = ctx.get<std::string>("--to-file", "");
  std::string starting_file_str =
      ctx.get<std::string>("-S", ctx.get<std::string>("--starting-file", ""));
  std::vector<std::string> exclude_pats;
  for (const auto &occ : ctx.string_occurrences({"-x", "--exclude"})) {
    exclude_pats.push_back(std::string(occ.value));
  }
  for (const auto &occ : ctx.string_occurrences({"-X", "--exclude-from"})) {
    auto lines = read_file_lines_result(std::string(occ.value));
    if (lines) {
      for (const auto &line : lines.value()) {
        if (!line.empty()) exclude_pats.push_back(line);
      }
    } else {
      safeErrorPrint("diff: ");
      safeErrorPrint(lines.error());
      safeErrorPrint("\n");
      return 2;
    }
  }
  std::vector<std::string> exclude_dir_pats;
  for (const auto &occ : ctx.string_occurrences({"--exclude-dir"})) {
    exclude_dir_pats.push_back(std::string(occ.value));
  }

  // -- Output format directives (-D/--ifdef, GFMT/LFMT family) --
  FormatDirectives formats;
  formats.changed_group = ctx.get<std::string>("--changed-group-format", "");
  formats.old_group = ctx.get<std::string>("--old-group-format", "");
  formats.new_group = ctx.get<std::string>("--new-group-format", "");
  formats.unchanged_group =
      ctx.get<std::string>("--unchanged-group-format", "");
  formats.line = ctx.get<std::string>("--line-format", "");
  formats.old_line = ctx.get<std::string>("--old-line-format", "");
  formats.new_line = ctx.get<std::string>("--new-line-format", "");
  formats.unchanged_line =
      ctx.get<std::string>("--unchanged-line-format", "");
  const bool use_ifdef = !ifdef_name.empty();
  const bool use_formats = directive_has_any(formats);
  if (use_ifdef) {
    formats = default_ifdef_formats(ifdef_name);
    if (auto v = ctx.get<std::string>("--changed-group-format", "");
        !v.empty()) {
      formats.changed_group = v;
    }
    if (auto v = ctx.get<std::string>("--old-group-format", ""); !v.empty()) {
      formats.old_group = v;
    }
    if (auto v = ctx.get<std::string>("--new-group-format", ""); !v.empty()) {
      formats.new_group = v;
    }
    if (auto v = ctx.get<std::string>("--unchanged-group-format", "");
        !v.empty()) {
      formats.unchanged_group = v;
    }
    if (auto v = ctx.get<std::string>("--line-format", ""); !v.empty()) {
      formats.line = v;
    }
  }
  cfg.formats = formats;

  enum class Mode {
    Normal,
    Unified,
    Context,
    SideBySide,
    Ed,
    ForwardEd,
    Rcs,
    Format
  };
  Mode mode = Mode::Normal;
  int context = 3;

  auto set_context = [&](int value, std::string_view option,
                         bool optional_value) -> bool {
    if (value < 0) {
      if (optional_value && value == -1) {
        context = 3;
        return true;
      }
      safeErrorPrint("diff: invalid context length '");
      safeErrorPrint(std::to_string(value));
      safeErrorPrint("'\n");
      safeErrorPrint("Try 'diff --help' for more information.\n");
      return false;
    }
    context = value;
    return true;
  };

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= ctx.metas->size()) continue;
    const auto &meta = (*ctx.metas)[occurrence.index];
    if (meta.short_name == "-u" || meta.long_name == "--unified") {
      mode = Mode::Unified;
      auto value = std::get_if<int>(&occurrence.value);
      if (value && !set_context(*value, "--unified", true)) return 1;
    } else if (meta.short_name == "-U") {
      mode = Mode::Unified;
      auto value = std::get_if<int>(&occurrence.value);
      if (value && !set_context(*value, "-U", false)) return 1;
    } else if (meta.short_name == "-c" || meta.long_name == "--context") {
      mode = Mode::Context;
      auto value = std::get_if<int>(&occurrence.value);
      if (value && !set_context(*value, "--context", true)) return 1;
    } else if (meta.short_name == "-C") {
      mode = Mode::Context;
      auto value = std::get_if<int>(&occurrence.value);
      if (value && !set_context(*value, "-C", false)) return 1;
    } else if (meta.short_name == "-y" || meta.long_name == "--side-by-side") {
      mode = Mode::SideBySide;
    } else if (meta.short_name == "-e" || meta.long_name == "--ed") {
      mode = Mode::Ed;
    } else if (meta.short_name == "-f" || meta.long_name == "--forward-ed") {
      mode = Mode::ForwardEd;
    } else if (meta.short_name == "-n" || meta.long_name == "--rcs") {
      mode = Mode::Rcs;
    } else if (meta.long_name == "--normal") {
      mode = Mode::Normal;
    }
  }
  auto to_pipeline_mode = [](Mode m) -> OutputMode {
    switch (m) {
      case Mode::Unified: return OutputMode::Unified;
      case Mode::Context: return OutputMode::Context;
      case Mode::SideBySide: return OutputMode::SideBySide;
      case Mode::Ed: return OutputMode::Ed;
      case Mode::ForwardEd: return OutputMode::ForwardEd;
      case Mode::Rcs: return OutputMode::Rcs;
      case Mode::Format: return OutputMode::Format;
      default: return OutputMode::Normal;
    }
  };
  // [GNU] -D/--ifdef and the GFMT/LFMT family generalize the line formats;
  // they take precedence over the fixed-format modes.
  if (use_ifdef || use_formats) mode = Mode::Format;
  cfg.output_mode = to_pipeline_mode(mode);
  cfg.context = context;

  // -p/-F function context only decorates unified/context headers.
  portable_regex::Pattern func_pattern_storage;
  std::string func_re_text = ctx.get<std::string>("-F", "");
  if (func_re_text.empty()) {
    func_re_text = ctx.get<std::string>("--show-function-line", "");
  }
  if (!func_re_text.empty()) {
    auto compiled =
        portable_regex::compile(portable_regex::Syntax::Basic, func_re_text);
    if (!compiled) {
      safeErrorPrint("diff: invalid regular expression '");
      safeErrorPrint(func_re_text);
      safeErrorPrint("'\n");
      return 2;
    }
    func_pattern_storage = std::move(compiled.pattern);
    cfg.style.func_re = &func_pattern_storage;
  } else if (ctx.has("-p") || ctx.has("--show-c-function")) {
    cfg.style.func_show_c = true;
  }

  bool recursive = ctx.has("-r") || ctx.has("--recursive");

  // --color[=WHEN]: plain --color means auto (color when stdout is a
  // terminal); 'always' forces it on, 'never' off.
  DiffColors colors;
  if (ctx.has("--color")) {
    std::string when = "auto";
    auto value = ctx.get<std::string>("--color", "");
    if (!value.empty()) when = value;
    if (when == "always") {
      colors.on = true;
    } else if (when == "auto") {
      colors.on = shouldUseAnsiColorStdout();
    }
  }
  cfg.style.colors = colors.on ? &colors : nullptr;

  // -l/--paginate buffers every diff line and paginates pr-style at the
  // end; error output stays unbuffered.
  bool paginate = ctx.has("-l") || ctx.has("--paginate");
  if (paginate) buffering_output = true;
  const std::string command_tail = diff_command_tail();
  auto finish = [&](int status) -> int {
    if (paginate) paginate_and_flush(command_tail);
    return status;
  };

  auto operand_is_directory = [](const std::string &operand_path) -> bool {
    if (operand_is_stdin(operand_path)) return false;
    auto operand = native_path::make_api_path_operand(operand_path);
    return native_path::attributes_are_directory(
        native_path::operand_target_attributes_w(operand));
  };

  // -- from-file/to-file: one side fixed, the other iterated --
  if (!from_file.empty() && !to_file.empty()) {
    safeErrorPrintLn(
        "diff: --from-file and --to-file are mutually exclusive");
    return 2;
  }
  if (!from_file.empty() || !to_file.empty()) {
    if (ctx.positionals.empty()) {
      safeErrorPrintLn("diff: missing operand");
      safeErrorPrintLn("Try 'diff --help' for more information.");
      return 2;
    }
    int worst = 0;
    for (const auto &positional : ctx.positionals) {
      std::string operand(positional);
      std::string a = from_file.empty() ? operand : from_file;
      std::string b = to_file.empty() ? operand : to_file;
      DiffLabel l1{a, false};
      DiffLabel l2{b, false};
      auto labels = ctx.string_occurrences({"--label"});
      if (!labels.empty()) l1 = DiffLabel{labels[0].value, true};
      if (labels.size() > 1) l2 = DiffLabel{labels[1].value, true};
      int status = compare_file_pair(a, b, l1, l2, cfg);
      if (status == 2) return finish(2);
      worst = std::max(worst, status);
    }
    return finish(worst);
  }

  auto files_result = resolve_files(ctx);
  if (!files_result) {
    safeErrorPrint("diff: ");
    safeErrorPrintLn(files_result.error());
    safeErrorPrint("Try 'diff --help' for more information.\n");
    return 2;
  }

  std::string file1 = (*files_result)[0];
  std::string file2 = (*files_result)[1];

  // [GNU] Two standard-input operands name the same stream: diff detects
  // the same inode and reports identical instead of consuming stdin twice.
  if (operand_is_stdin(file1) && operand_is_stdin(file2)) {
    if (file_io::stdin_is_bad()) {
      dput("diff: ");
      dput(file1);
      dput(": Bad file descriptor\n");
      return finish(2);
    }
    if (cfg.report_identical) {
      dput("Files ");
      dput(file1);
      dput(" and ");
      dput(file2);
      dput(" are identical\n");
    }
    return finish(0);
  }

  // [GNU] A directory operand is rewritten to "<dir>/<basename of the
  // other operand>" before comparison, so "diff dir file" compares
  // dir/file with file.
  auto base_name_of = [](const std::string &operand_path) -> std::string {
    size_t end = operand_path.size();
    while (end > 0 &&
           (operand_path[end - 1] == '/' || operand_path[end - 1] == '\\')) {
      --end;
    }
    size_t start = end;
    while (start > 0 && operand_path[start - 1] != '/' &&
           operand_path[start - 1] != '\\') {
      --start;
    }
    return operand_path.substr(start, end - start);
  };
  auto join_dir_member = [](const std::string &dir,
                            const std::string &member) -> std::string {
    if (member.empty()) return dir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
      return dir + "/" + member;
    }
    return dir + member;
  };
  const bool file1_is_dir = operand_is_directory(file1);
  const bool file2_is_dir = operand_is_directory(file2);
  if (file1_is_dir && !file2_is_dir) {
    file1 = join_dir_member(file1, base_name_of(file2));
  } else if (file2_is_dir && !file1_is_dir) {
    file2 = join_dir_member(file2, base_name_of(file1));
  }

  // -- Two directory operands: per-level listing, -r recursion --
  if (file1_is_dir && file2_is_dir) {
    int status = compare_directory_pair(
        file1, file2, cfg, recursive, exclude_pats, exclude_dir_pats,
        starting_file_str, command_tail);
    return finish(status);
  }

  DiffLabel label1{file1, false};
  DiffLabel label2{file2, false};
  auto labels = ctx.string_occurrences({"--label"});
  if (!labels.empty()) label1 = DiffLabel{labels[0].value, true};
  if (labels.size() > 1) label2 = DiffLabel{labels[1].value, true};

  int status = compare_file_pair(file1, file2, label1, label2, cfg);
  return finish(status);
}
