// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Shared line-diff engine for the diff command family.
///     diff, diff3 and sdiff all build on the same LCS edit script and
///     the same GNU diffutils side-by-side line printer, so the family
///     stays byte-compatible with GNU diffutils 3.10 as one unit.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#pragma once

#include "pch/pch.h"

namespace diff_engine {

// ===========================================================================
// Edit script core (LCS over lines)
// ===========================================================================

enum class EditType { KEEP, DEL, INS };

struct Edit {
  EditType type;
  size_t line1_index;  // Line index in file1 (for DEL/KEEP)
  size_t line2_index;  // Line index in file2 (for INS/KEEP)
};

inline auto is_identical(const std::vector<std::string> &lines1,
                         const std::vector<std::string> &lines2) -> bool {
  if (lines1.size() != lines2.size()) {
    return false;
  }
  return lines1 == lines2;
}

inline auto compute_lcs_optimized(const std::vector<std::string> &lines1,
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

inline auto backtrack_lcs(const std::vector<std::vector<size_t>> &lcs,
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

inline auto compute_diff(const std::vector<std::string> &lines1,
                         const std::vector<std::string> &lines2)
    -> std::vector<Edit> {
  // Fast path: identical files
  if (is_identical(lines1, lines2)) {
    return {};
  }

  auto lcs = compute_lcs_optimized(lines1, lines2);
  return backtrack_lcs(lcs, lines1, lines2);
}

// ===========================================================================
// Line normalization for the ignore-option family
// ===========================================================================

inline auto normalize_line_for_compare(const std::string &line,
                                       bool ignore_all_space) -> std::string {
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

inline auto normalize_lines_for_compare(const std::vector<std::string> &lines,
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

inline auto strip_trailing_cr_lines(const std::vector<std::string> &lines)
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

inline auto filter_blank_lines(const std::vector<std::string> &lines)
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

// [GNU diffutils io.c find_and_hash_each_line, IGNORE_SPACE_CHANGE]:
// each whitespace run collapses to a single space, but a run that reaches
// end of line is dropped entirely - trailing white space vanishes.
inline auto normalize_space_change(const std::string &line) -> std::string {
  std::string result;
  result.reserve(line.size());
  bool in_space = false;
  for (char ch : line) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      in_space = true;
      continue;
    }
    if (in_space) {
      result.push_back(' ');
      in_space = false;
    }
    result.push_back(ch);
  }
  return result;
}

inline auto normalize_lines_space_change(const std::vector<std::string> &lines)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  result.reserve(lines.size());
  for (const auto &line : lines) {
    result.push_back(normalize_space_change(line));
  }
  return result;
}

inline auto normalize_lines_case(const std::vector<std::string> &lines)
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

// [GNU -Z/--ignore-trailing-space]: white space at line end vanishes
// (isspace set, so a trailing CR vanishes with it).
inline auto strip_trailing_space_lines(const std::vector<std::string> &lines)
    -> std::vector<std::string> {
  std::vector<std::string> out;
  out.reserve(lines.size());
  for (const auto &line : lines) {
    size_t end = line.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1]))) {
      --end;
    }
    out.push_back(line.substr(0, end));
  }
  return out;
}

// [GNU -E/--ignore-tab-expansion]: tabs compare as spaces up to the next
// tab stop; a CR resets the column.  Everything else (including the CR
// itself) compares literally.
inline auto normalize_tab_expansion_lines(const std::vector<std::string> &lines,
                                          int tabsize)
    -> std::vector<std::string> {
  std::vector<std::string> out;
  out.reserve(lines.size());
  for (const auto &line : lines) {
    std::string normalized;
    normalized.reserve(line.size());
    size_t column = 0;
    for (char ch : line) {
      if (ch == '\t') {
        size_t spaces = static_cast<size_t>(tabsize) -
                        column % static_cast<size_t>(tabsize);
        normalized.append(spaces, ' ');
        column += spaces;
      } else {
        if (ch == '\r') {
          column = 0;
        } else {
          ++column;
        }
        normalized.push_back(ch);
      }
    }
    out.push_back(std::move(normalized));
  }
  return out;
}

// [GNU diffutils io.c] A file whose last line has no newline puts that
// line in a separate equivalence class: it compares equal only to the
// other file's incomplete last line.  Modes that strip trailing white
// space (-Z/-b/-W) make the newline irrelevant and disable the rule.
inline void mark_incomplete_last_line(std::vector<std::string> &lines,
                                      bool ends_with_newline,
                                      bool newline_insensitive) {
  if (!ends_with_newline && !newline_insensitive && !lines.empty()) {
    lines.back().push_back('\x01');
  }
}

inline auto expand_tabs_in_line(const std::string &line, int tabsize)
    -> std::string {
  std::string result;
  int col = 0;
  for (char ch : line) {
    if (ch == '\t') {
      int spaces = tabsize - (col % tabsize);
      result.append(static_cast<size_t>(spaces), ' ');
      col += spaces;
    } else {
      result.push_back(ch);
      ++col;
    }
  }
  return result;
}

inline auto expand_tabs_lines(const std::vector<std::string> &lines,
                              int tabsize) -> std::vector<std::string> {
  std::vector<std::string> result;
  result.reserve(lines.size());
  for (const auto &line : lines) {
    result.push_back(expand_tabs_in_line(line, tabsize));
  }
  return result;
}

// The compiled -I/--ignore-matching-lines RE set (#1449).  GNU
// diffutils compiles each -I argument as a basic regular expression
// and searches every changed line with it; a change hunk is ignored
// when all of its lines match at least one of the REs.  The patterns
// live in the repo's shared portable_regex engine, with the same
// Syntax::Basic choice the family already makes for
// --show-function-line and tac -r.
struct IgnoreMatchingPatterns {
  std::vector<portable_regex::Pattern> patterns;

  [[nodiscard]] auto empty() const -> bool { return patterns.empty(); }

  [[nodiscard]] auto matches_line(const std::string &line) const -> bool {
    for (const auto &pattern : patterns) {
      if (pattern.find_first(line)) return true;
    }
    return false;
  }
};

// Compiles the -I argument texts; on failure the offending text is
// returned so the caller can diagnose it (GNU exits 2 before touching
// the operands).
inline auto compile_ignore_matching_patterns(
    const std::vector<std::string> &texts)
    -> std::expected<IgnoreMatchingPatterns, std::string> {
  IgnoreMatchingPatterns compiled;
  for (const auto &text : texts) {
    auto result =
        portable_regex::compile(portable_regex::Syntax::Basic, text);
    if (!result) return std::unexpected(text);
    compiled.patterns.push_back(std::move(result.pattern));
  }
  return compiled;
}

// -I/--ignore-matching-lines: drop the lines no -I RE matches.
inline auto filter_matching_lines(const std::vector<std::string> &lines,
                                  const IgnoreMatchingPatterns &matching)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  for (const auto &line : lines) {
    if (!matching.matches_line(line)) {
      result.push_back(line);
    }
  }
  return result;
}

inline auto line_is_blank(const std::string &line) -> bool {
  for (char ch : line) {
    if (!std::isspace(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

// [GNU analyze.c discard_confusing_lines] -B/--ignore-blank-lines and
// -I/--ignore-matching-lines diff the retained lines and drop the changes
// that the ignore rules reject; the rejected lines reattach to the
// unchanged flow and realign positionally.  hunk_is_ignored inspects the
// original lines spanned by edits[begin, end).  The realigned script keeps
// original line indices, so every output format still reads the
// unfiltered buffers.
inline auto drop_ignored_hunks(
    const std::vector<Edit> &edits, const std::vector<std::string> &lines1,
    const std::vector<std::string> &lines2,
    const std::function<bool(
        const std::vector<Edit> &, const std::vector<std::string> &,
        const std::vector<std::string> &, size_t, size_t)> &hunk_is_ignored)
    -> std::vector<Edit> {
  if (!hunk_is_ignored) {
    return edits;
  }
  std::vector<Edit> result;
  result.reserve(edits.size());
  std::vector<size_t> pending_l;  // ignored lines waiting for a common run
  std::vector<size_t> pending_r;
  size_t k = 0;
  while (k < edits.size()) {
    if (edits[k].type == EditType::KEEP) {
      size_t start = k;
      while (k < edits.size() && edits[k].type == EditType::KEEP) {
        ++k;
      }
      // Re-pair the run's slots behind any ignored lines queued before it.
      std::vector<size_t> left = pending_l;
      std::vector<size_t> right = pending_r;
      pending_l.clear();
      pending_r.clear();
      for (size_t i = start; i < k; ++i) {
        left.push_back(edits[i].line1_index);
        right.push_back(edits[i].line2_index);
      }
      const size_t paired = std::min(left.size(), right.size());
      for (size_t i = 0; i < paired; ++i) {
        result.push_back({EditType::KEEP, left[i], right[i]});
      }
      pending_l.assign(left.begin() + static_cast<long>(paired), left.end());
      pending_r.assign(right.begin() + static_cast<long>(paired), right.end());
      continue;
    }
    size_t start = k;
    while (k < edits.size() && edits[k].type != EditType::KEEP) {
      ++k;
    }
    if (hunk_is_ignored(edits, lines1, lines2, start, k)) {
      // Queued for positional realignment with the next common run.
      for (size_t i = start; i < k; ++i) {
        (edits[i].type == EditType::DEL ? pending_l : pending_r)
            .push_back(edits[i].type == EditType::DEL ? edits[i].line1_index
                                                      : edits[i].line2_index);
      }
      continue;
    }
    result.insert(result.end(), edits.begin() + static_cast<long>(start),
                  edits.begin() + static_cast<long>(k));
  }
  // Unabsorbed ignored lines stay unconsumed: the display tail pairs them
  // positionally with whatever common lines remain on the other side.
  return result;
}

// The ignored-hunk test shared by -B and -I, in GNU's per-line
// invisibility shape (analyze.c): a hunk is ignored when every changed
// line is ignored, where a line is ignored iff -B covers it (the line
// is blank) or an -I RE matches it (#1449).  The two rules mix freely
// inside one hunk, and neither covers the other's lines: a blank line
// is not ignored by -I alone, nor a matching line by -B alone.
inline auto make_blank_or_matching_predicate(
    bool ignore_blank_lines, const IgnoreMatchingPatterns &matching)
    -> std::function<bool(const std::vector<Edit> &,
                          const std::vector<std::string> &,
                          const std::vector<std::string> &, size_t, size_t)> {
  return [ignore_blank_lines, matching](
             const std::vector<Edit> &edits,
             const std::vector<std::string> &lines1,
             const std::vector<std::string> &lines2, size_t begin,
             size_t end) {
    for (size_t i = begin; i < end; ++i) {
      const std::string &line = edits[i].type == EditType::DEL
                                    ? lines1[edits[i].line1_index]
                                    : lines2[edits[i].line2_index];
      const bool ignored = (ignore_blank_lines && line_is_blank(line)) ||
                           matching.matches_line(line);
      if (!ignored) return false;
    }
    return true;
  };
}

// ===========================================================================
// Ed-style hunks (used by diff -e/-f/-n and by diff3's two-way threads)
// ===========================================================================

struct ScriptHunk {
  size_t old_start = 0;  // 1-based, inclusive
  size_t old_count = 0;
  size_t new_start = 0;  // 1-based, inclusive
  size_t new_count = 0;
  size_t ins_anchor = 0;  // old-file line the insertion follows (0 = prepend)
  std::vector<size_t> del_idx;  // indices into lines1
  std::vector<size_t> ins_idx;  // indices into lines2
};

inline auto build_script_hunks(const std::vector<Edit> &edits)
    -> std::vector<ScriptHunk> {
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

// ===========================================================================
// Input files
// ===========================================================================

// [GNU] "-" (and /dev/stdin-family operands) read standard input (#1057).
inline auto operand_is_stdin(const std::string &path) -> bool {
  return path == "-" ||
         native_path::pseudo_device_std_fd(path) == std::optional<int>(0);
}

// The operand text GNU echoes back: the "diff -ru a b" tail in -r headers
// and the -l page header, and the last operand in "missing operand after
// '%s'". The dispatcher already parsed the process command line and hands
// it to every command as ctx.raw_args (argv[0] and the multiplexer verb
// token already dropped), so these are computed from that span. The diff
// family used to re-run CommandLineToArgvW on GetCommandLineW() once per
// command to rebuild the same tokens - three hand-copied copies, two of
// them verbatim duplicates - which also read the *host's* command line
// when a command runs inside another process (FFI DLL form).
inline auto command_tail_from_raw_args(
    std::string_view prog, std::span<const std::string_view> raw_args)
    -> std::string {
  std::string tail{prog};
  for (const auto arg : raw_args) {
    tail += " ";
    tail += arg;
  }
  return tail;
}

inline auto last_raw_argument(std::string_view prog,
                              std::span<const std::string_view> raw_args)
    -> std::string {
  return raw_args.empty() ? std::string{prog} : std::string{raw_args.back()};
}

struct FileLines {
  std::vector<std::string> lines;
  bool ends_with_newline = false;  // final line was '\n'-terminated
};

inline auto split_into_lines(const std::string &all) -> FileLines {
  FileLines result;
  if (all.empty()) return result;
  result.ends_with_newline = all.back() == '\n';
  size_t start = 0;
  while (start < all.size()) {
    size_t pos = all.find('\n', start);
    if (pos == std::string::npos) {
      result.lines.emplace_back(all.substr(start));
      break;
    }
    result.lines.emplace_back(all.substr(start, pos - start));
    start = pos + 1;
  }
  return result;
}

// Read a file (or stdin) as binary, splitting on '\n' without keeping the
// newline in the line contents.  The trailing-newline flag is what GNU
// encodes by keeping '\n' inside the line buffers.
inline auto read_file_lines_result(const std::string &path)
    -> std::expected<FileLines, std::string> {
  auto diff_input_open_error = [](std::string_view file_path) -> std::string {
    auto operand = native_path::make_api_path_operand(file_path);
    const DWORD attrs = native_path::operand_target_attributes_w(operand);
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        native_path::attributes_are_directory(attrs)) {
      return std::string(file_path) + ": Is a directory";
    }
    // [GNU] Unreadable input reports "<path>: <errno text>", not a
    // "cannot open ... for reading" wrapper.
    return std::string(file_path) + ": No such file or directory";
  };

  if (operand_is_stdin(path)) {
    // [GNU] A closed standard input (<&-) is a read error, not EOF (#973).
    if (file_io::stdin_is_bad()) {
      return std::unexpected(path + ": Bad file descriptor");
    }
    std::string all((std::istreambuf_iterator<char>(std::cin)),
                    std::istreambuf_iterator<char>());
    return split_into_lines(all);
  }

  std::ifstream file = file_io::open_binary_file(path);
  if (!file.is_open()) {
    return std::unexpected(diff_input_open_error(path));
  }

  std::string all((std::istreambuf_iterator<char>(file)),
                  std::istreambuf_iterator<char>());
  return split_into_lines(all);
}

// True when the operand names an openable non-directory (used by diff3 to
// report missing inputs the way the subsidiary diff would).
inline auto operand_is_readable_file(const std::string &path) -> bool {
  if (operand_is_stdin(path)) return true;
  auto operand = native_path::make_api_path_operand(path);
  const DWORD attrs = native_path::operand_target_attributes_w(operand);
  return attrs != INVALID_FILE_ATTRIBUTES &&
         !native_path::attributes_are_directory(attrs);
}

inline auto operand_target_is_directory(const std::string &path) -> bool {
  if (operand_is_stdin(path)) return false;
  auto operand = native_path::make_api_path_operand(path);
  return native_path::attributes_are_directory(
      native_path::operand_target_attributes_w(operand));
}

// ===========================================================================
// GNU diffutils side.c: side-by-side printer shared by diff -y and sdiff
// ===========================================================================

struct SdiffColumns {
  size_t half_width = 0;        // left-column print bound (gutter width - 3)
  size_t column2_offset = 0;    // where the right column starts
  size_t separator_column = 0;  // where | < > ( ) land
};

// [GNU diffutils diff.c] The gutter is at least 3 columns and, when tabs
// are kept literal, column2_offset is an integral number of tab stops so
// right-column tabs line up across rows.
inline auto sdiff_columns(int width, int tabsize, bool expand_tabs)
    -> SdiffColumns {
  const size_t t =
      expand_tabs ? 1 : static_cast<size_t>(tabsize > 0 ? tabsize : 8);
  const size_t w = static_cast<size_t>(width > 0 ? width : 0);
  const size_t gutter_minimum = 3;
  const size_t t_plus_g = t + gutter_minimum;
  const size_t unaligned_off = (w >> 1) + (t_plus_g >> 1) + (w & t_plus_g & 1);
  const size_t off = unaligned_off - unaligned_off % t;
  SdiffColumns cols;
  if (off <= gutter_minimum || w <= off) {
    cols.half_width = 0;
    cols.column2_offset = w;
  } else {
    cols.half_width = std::min(off - gutter_minimum, w - off);
    cols.column2_offset = off;
  }
  cols.separator_column = (cols.half_width + cols.column2_offset - 1) / 2;
  return cols;
}

struct SdiffStyle {
  int tabsize = 8;
  bool expand_tabs = false;
};

// Tab from column FROM to column TO (from <= to), tab characters first
// unless tabs are expanded, then spaces.
inline void sdiff_tab_from_to(size_t from, size_t to, const SdiffStyle &style,
                              const std::string &indent_unit,
                              std::string &out) {
  if (!style.expand_tabs) {
    for (size_t tab = from + style.tabsize - from % style.tabsize; tab <= to;
         tab += style.tabsize) {
      out += "\t";
      from = tab;
    }
  }
  while (from++ < to) out += indent_unit;
}

// Print the text of one line bounded to out_bound print columns, observing
// tabs (GNU side.c print_half_line).  `line` carries its own flag telling
// whether it was newline-terminated in the input file; the flag only
// matters to the caller (sep selection / trailing newline), not here.
inline auto sdiff_print_half_line(const std::string &line, size_t indent,
                                  size_t out_bound, const SdiffStyle &style,
                                  std::string &out) -> size_t {
  size_t in_position = 0;
  size_t out_position = 0;
  auto put = [&](std::string_view text) { out += text; };

  size_t i = 0;
  while (i < line.size()) {
    char c = line[i++];
    switch (c) {
      case '\t': {
        size_t spaces = style.tabsize - in_position % style.tabsize;
        if (in_position == out_position) {
          size_t tabstop = out_position + spaces;
          if (style.expand_tabs) {
            if (out_bound < tabstop) tabstop = out_bound;
            for (; out_position < tabstop; out_position++) put(" ");
          } else if (tabstop < out_bound) {
            out_position = tabstop;
            put("\t");
          }
        }
        in_position += spaces;
        break;
      }
      case '\r':
        put("\r");
        {
          std::string pad;
          sdiff_tab_from_to(0, indent, style, " ", pad);
          put(pad);
        }
        in_position = out_position = 0;
        break;
      case '\b':
        if (in_position != 0 && --in_position < out_bound) {
          if (out_position <= in_position) {
            // Add spaces to make up for a suppressed tab past out_bound.
            for (; out_position < in_position; out_position++) put(" ");
          } else {
            out_position = in_position;
            put("\b");
          }
        }
        break;
      case '\f':
      case '\v':
        // GNU emits these without advancing either column.
        if (in_position < out_bound) {
          out += std::string(1, c);
        }
        break;
      case '\n':
        return out_position;
      default:
        // Bytes count one print column each (C-locale semantics).
        if (in_position < out_bound) {
          out += std::string(1, c);
          out_position = in_position + 1;
        }
        ++in_position;
        break;
    }
  }
  return out_position;
}

// One side-by-side row (GNU print_1sdiff_line).  left/right are line
// contents (no trailing newline); a nullopt side prints as blank space.
inline void sdiff_emit_row(const std::optional<std::string> &left,
                           bool left_has_newline, char sep,
                           const std::optional<std::string> &right,
                           bool right_has_newline, const SdiffColumns &cols,
                           const SdiffStyle &style, std::string &out) {
  size_t col = 0;
  bool put_newline = false;

  if (left) {
    put_newline = left_has_newline;
    col = sdiff_print_half_line(*left, 0, cols.half_width, style, out);
  }

  if (sep != ' ') {
    std::string pad;
    sdiff_tab_from_to(col, cols.separator_column, style, " ", pad);
    out += pad;
    col = cols.separator_column;
    char effective = sep;
    if (sep == '|' && put_newline != right_has_newline) {
      effective = put_newline ? '/' : '\\';
    }
    out += effective;
    col += 1;
  }

  if (right) {
    put_newline = put_newline || right_has_newline;
    // [GNU] An empty right line skips the whole right half, padding
    // included.
    if (!right->empty()) {
      std::string pad;
      sdiff_tab_from_to(col, cols.column2_offset, style, " ", pad);
      out += pad;
      col = cols.column2_offset;
      sdiff_print_half_line(*right, col, cols.half_width, style, out);
    }
  }

  if (put_newline) out += "\n";
}

// One positional section of a side-by-side script: either a common span
// (the raw line ranges between two change hunks, in each file
// independently) or one change hunk (its deleted range in file1 and its
// inserted range in file2).  GNU side.c walks the script in exactly
// these sections: print_sdiff_common_lines renders the span between the
// previous hunk and the next one positionally, so lines of a change
// that an ignore option (-B/-I) removed from the script still occupy
// display rows inside the surrounding common span (paired with whatever
// line faces them, or as '('/ ')' one-sided rows) instead of being
// re-paired into the script's common flow.
struct SdiffSection {
  bool is_hunk = false;
  size_t left_begin = 0;   // first line index in file1
  size_t left_end = 0;     // one past the last line index in file1
  size_t right_begin = 0;  // first line index in file2
  size_t right_end = 0;    // one past the last line index in file2
};

// Append one section's side-by-side rows to `out` (GNU side.c
// print_sdiff_common_lines for a common span, print_sdiff_hunk for a
// change hunk).  Common spans pair their lines positionally: paired
// rows carry the ' ' separator, a right-only remainder prints with ')'
// and a left-only remainder with '('; with left_column every left line
// prints with '(' and the right side is not shown.  Change hunks pair
// min(deletes, inserts) rows with '|', extra deletes print with '<'
// and extra inserts with '>'.
inline void append_sdiff_section_rows(const SdiffSection &section,
                                      const std::vector<std::string> &lines1,
                                      const std::vector<std::string> &lines2,
                                      bool ends_with_newline1,
                                      bool ends_with_newline2,
                                      const SdiffColumns &cols,
                                      const SdiffStyle &style,
                                      bool left_column, std::string &out) {
  auto has_nl1 = [&](size_t idx) {
    return idx + 1 < lines1.size() || ends_with_newline1;
  };
  auto has_nl2 = [&](size_t idx) {
    return idx + 1 < lines2.size() || ends_with_newline2;
  };

  if (!section.is_hunk) {
    size_t i0 = section.left_begin;
    size_t i1 = section.right_begin;
    if (!left_column) {
      while (i0 < section.left_end && i1 < section.right_end) {
        sdiff_emit_row(lines1[i0], has_nl1(i0), ' ', lines2[i1], has_nl2(i1),
                       cols, style, out);
        ++i0;
        ++i1;
      }
      while (i1 < section.right_end) {
        sdiff_emit_row(std::nullopt, false, ')', lines2[i1], has_nl2(i1), cols,
                       style, out);
        ++i1;
      }
    }
    while (i0 < section.left_end) {
      sdiff_emit_row(lines1[i0], has_nl1(i0), '(', std::nullopt, false, cols,
                     style, out);
      ++i0;
    }
    return;
  }

  const size_t dels = section.left_end - section.left_begin;
  const size_t inss = section.right_end - section.right_begin;
  const size_t paired = std::min(dels, inss);
  for (size_t r = 0; r < paired; ++r) {
    sdiff_emit_row(lines1[section.left_begin + r],
                   has_nl1(section.left_begin + r), '|',
                   lines2[section.right_begin + r],
                   has_nl2(section.right_begin + r), cols, style, out);
  }
  for (size_t r = paired; r < dels; ++r) {
    sdiff_emit_row(lines1[section.left_begin + r],
                   has_nl1(section.left_begin + r), '<', std::nullopt, false,
                   cols, style, out);
  }
  for (size_t r = paired; r < inss; ++r) {
    sdiff_emit_row(std::nullopt, false, '>', lines2[section.right_begin + r],
                   has_nl2(section.right_begin + r), cols, style, out);
  }
}

// ======================================================
// Positional sections over the raw edit script
// ======================================================

// GNU renders and merges from the change script AFTER the ignore
// options have removed the hunks they cover (analyze.c): an ignored
// hunk's lines stop being changes and join the common span around
// them, pairing positionally there (side.c print_sdiff_common_lines).
// A 1:1 edit list cannot express that asymmetric span, so the section
// builder works directly from the raw script: consecutive non-KEEP
// runs become hunks (or vanish into the common flow when the ignore
// predicate covers them), and each common span is the raw line range
// between the surviving hunks, in each file independently.
struct SdiffScript {
  std::vector<SdiffSection> sections;
  bool differ = false;  // a non-ignored hunk exists
};

inline auto build_sdiff_script(
    const std::vector<Edit> &edits, const std::vector<std::string> &lines1,
    const std::vector<std::string> &lines2,
    const std::function<bool(const std::vector<Edit> &,
                             const std::vector<std::string> &,
                             const std::vector<std::string> &, size_t, size_t)>
        &hunk_is_ignored) -> SdiffScript {
  SdiffScript script;
  size_t common_left = 0;
  size_t common_right = 0;
  size_t k = 0;
  auto flush_common = [&](size_t left_end, size_t right_end) {
    if (left_end == common_left && right_end == common_right) return;
    SdiffSection section;
    section.is_hunk = false;
    section.left_begin = common_left;
    section.left_end = left_end;
    section.right_begin = common_right;
    section.right_end = right_end;
    script.sections.push_back(section);
    common_left = left_end;
    common_right = right_end;
  };
  while (k < edits.size()) {
    if (edits[k].type == EditType::KEEP) {
      ++k;
      continue;
    }
    const size_t run_begin = k;
    // A change run's ranges: the DEL indices are contiguous in file1
    // and the INS indices in file2; a side with no edits anchors at
    // the other side's cursor in its own file.
    size_t left_begin = edits[k].line1_index;
    size_t left_end = left_begin;
    size_t right_begin = edits[k].line2_index;
    size_t right_end = right_begin;
    while (k < edits.size() && edits[k].type != EditType::KEEP) {
      if (edits[k].type == EditType::DEL) {
        left_end = edits[k].line1_index + 1;
      } else {
        right_end = edits[k].line2_index + 1;
      }
      ++k;
    }
    if (hunk_is_ignored &&
        hunk_is_ignored(edits, lines1, lines2, run_begin, k)) {
      continue;  // the run's lines join the surrounding common span
    }
    flush_common(left_begin, right_begin);
    SdiffSection hunk;
    hunk.is_hunk = true;
    hunk.left_begin = left_begin;
    hunk.left_end = left_end;
    hunk.right_begin = right_begin;
    hunk.right_end = right_end;
    script.sections.push_back(hunk);
    script.differ = true;
    common_left = left_end;
    common_right = right_end;
  }
  flush_common(lines1.size(), lines2.size());
  return script;
}

// Print the whole edit script as side-by-side rows (GNU side.c
// print_sdiff_script).  Common lines print once per row with the right
// copy aligned at column2_offset; changes pair min(deletes, inserts) rows
// with '|', extra deletes '<' and extra inserts '>'.
inline void emit_side_by_side(
    const std::vector<Edit> &edits, const std::vector<std::string> &lines1,
    const std::vector<std::string> &lines2, bool ends_with_newline1,
    bool ends_with_newline2, int width, bool suppress_common_lines,
    bool left_column, const SdiffStyle &style,
    const std::function<void(std::string_view)> &sink) {
  const SdiffColumns cols =
      sdiff_columns(width, style.tabsize, style.expand_tabs);
  auto has_nl1 = [&](size_t idx) {
    return idx + 1 < lines1.size() || ends_with_newline1;
  };
  auto has_nl2 = [&](size_t idx) {
    return idx + 1 < lines2.size() || ends_with_newline2;
  };

  std::string row;
  auto flush_row = [&]() {
    sink(row);
    row.clear();
  };

  auto emit_common = [&](size_t a, size_t b) {
    if (left_column) {
      // [GNU] --left-column prints the common line once and marks the
      // suppressed right column with '('.
      sdiff_emit_row(lines1[a], has_nl1(a), '(', std::nullopt, false, cols,
                     style, row);
    } else {
      sdiff_emit_row(lines1[a], has_nl1(a), ' ', lines2[b], has_nl2(b), cols,
                     style, row);
    }
    flush_row();
  };
  // [GNU print_sdiff_common_lines] Common runs pair both files line by
  // line; a right-only remainder prints with ')', a left-only one with '('.
  auto emit_common_range = [&](size_t begin0, size_t limit0, size_t begin1,
                               size_t limit1) {
    size_t i0 = begin0;
    size_t i1 = begin1;
    if (!left_column) {
      while (i0 != limit0 && i1 != limit1) {
        emit_common(i0++, i1++);
      }
      while (i1 != limit1) {
        sdiff_emit_row(std::nullopt, false, ')', lines2[i1], has_nl2(i1), cols,
                       style, row);
        flush_row();
        ++i1;
      }
    }
    while (i0 != limit0) {
      sdiff_emit_row(lines1[i0], has_nl1(i0), '(', std::nullopt, false, cols,
                     style, row);
      flush_row();
      ++i0;
    }
  };

  size_t k = 0;
  if (edits.empty()) {
    // Identical inputs: GNU still prints every common line (unless -s).
    if (!suppress_common_lines) {
      emit_common_range(0, lines1.size(), 0, lines2.size());
    }
    return;
  }

  while (k < edits.size()) {
    if (edits[k].type == EditType::KEEP) {
      while (k < edits.size() && edits[k].type == EditType::KEEP) {
        if (!suppress_common_lines) {
          emit_common(edits[k].line1_index, edits[k].line2_index);
        }
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
    for (size_t r = 0; r < paired; ++r) {
      sdiff_emit_row(lines1[dels[r]], has_nl1(dels[r]), '|', lines2[ins[r]],
                     has_nl2(ins[r]), cols, style, row);
      flush_row();
    }
    for (size_t r = paired; r < dels.size(); ++r) {
      sdiff_emit_row(lines1[dels[r]], has_nl1(dels[r]), '<', std::nullopt,
                     false, cols, style, row);
      flush_row();
    }
    for (size_t r = paired; r < ins.size(); ++r) {
      sdiff_emit_row(std::nullopt, false, '>', lines2[ins[r]], has_nl2(ins[r]),
                     cols, style, row);
      flush_row();
    }
  }

  // [GNU print_sdiff_common_lines] The tail after the last script element
  // pairs positionally; dropped (-B/-I) changes leave their lines to this
  // realignment.
  size_t last1 = 0;
  size_t last2 = 0;
  for (const auto &edit : edits) {
    if (edit.type != EditType::INS)
      last1 = std::max(last1, edit.line1_index + 1);
    if (edit.type != EditType::DEL)
      last2 = std::max(last2, edit.line2_index + 1);
  }
  if (last1 < lines1.size() || last2 < lines2.size()) {
    if (suppress_common_lines) return;
    emit_common_range(last1, lines1.size(), last2, lines2.size());
  }
}

}  // namespace diff_engine
