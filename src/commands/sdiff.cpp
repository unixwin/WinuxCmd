// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sdiff command.
///     Side-by-side merge of file differences following GNU diffutils
///     3.10: the display is exactly `diff --side-by-side` output (GNU
///     sdiff execs diff for it), and -o writes a merged file.  Without a
///     terminal on stdin GNU sdiff would prompt per change and quit on
///     EOF; this implementation auto-merges instead (issue #1127 P1):
///     clean changes take their only side, overlapping changes keep the
///     left column.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#endif

import std;
import core;
import utils;

#include "diff_engine.h"

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

// [GNU 3.10] wording follows `sdiff --help` verbatim.
auto constexpr SDIFF_OPTIONS = std::array{
    OPTION("-o", "--output", "operate interactively, sending output to FILE",
           STRING_TYPE),
    OPTION("-i", "--ignore-case",
           "consider upper- and lower-case to be the same"),
    OPTION("-E", "--ignore-tab-expansion",
           "ignore changes due to tab expansion"),
    OPTION("-Z", "--ignore-trailing-space", "ignore white space at line end"),
    OPTION("-b", "--ignore-space-change",
           "ignore changes in the amount of white space"),
    OPTION("-W", "--ignore-all-space", "ignore all white space"),
    OPTION("-B", "--ignore-blank-lines",
           "ignore changes whose lines are all blank"),
    OPTION("-I", "--ignore-matching-lines",
           "ignore changes all whose lines match RE", STRING_TYPE),
    OPTION("", "--strip-trailing-cr",
           "strip trailing carriage return on input"),
    OPTION("-a", "--text", "treat all files as text"),
    OPTION("-w", "--width", "output at most NUM (default 130) print columns",
           INT_TYPE),
    OPTION("-l", "--left-column",
           "output only the left column of common lines"),
    OPTION("-s", "--suppress-common-lines", "do not output common lines"),
    OPTION("-t", "--expand-tabs", "expand tabs to spaces in output"),
    OPTION("", "--tabsize", "tab stops at every NUM (default 8) print columns",
           INT_TYPE),
    OPTION("-d", "--minimal", "try hard to find a smaller set of changes"),
    OPTION("-H", "--speed-large-files",
           "assume large files, many scattered small changes"),
    OPTION("", "--diff-program", "use PROGRAM to compare files", STRING_TYPE)};

// ======================================================
// Main command implementation
// ======================================================

namespace sdiff_pipeline {

namespace de = diff_engine;

struct SdiffConfig {
  std::string output_file;
  bool left_column = false;
  bool suppress_common = false;
  int width = 130;
  int tabsize = 8;
  bool expand_tabs = false;
  bool ignore_case = false;
  bool ignore_all_space = false;
  bool ignore_space_change = false;
  bool ignore_blank_lines = false;
  bool ignore_trailing_space = false;
  bool ignore_tab_expansion = false;
  bool strip_trailing_cr = false;
  std::string ignore_matching;
};

auto resolve_files(const CommandContext<SDIFF_OPTIONS.size()> &ctx)
    -> std::expected<std::pair<std::string, std::string>, std::string> {
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

  return std::make_pair(files[0], files[1]);
}

// Apply the ignore-option family for matching only; display always shows
// the original lines, exactly like the forwarded GNU diff options.
auto normalize_for_compare(const std::vector<std::string> &lines,
                           const SdiffConfig &cfg) -> std::vector<std::string> {
  std::vector<std::string> out = lines;
  if (cfg.strip_trailing_cr) {
    out = de::strip_trailing_cr_lines(out);
  }
  if (cfg.ignore_trailing_space) {
    out = de::strip_trailing_space_lines(out);
  }
  if (cfg.ignore_tab_expansion) {
    out = de::normalize_tab_expansion_lines(out, cfg.tabsize);
  }
  out = de::normalize_lines_for_compare(out, cfg.ignore_all_space);
  if (cfg.ignore_space_change) {
    out = de::normalize_lines_space_change(out);
  }
  if (cfg.ignore_case) {
    out = de::normalize_lines_case(out);
  }
  // [GNU] -B/-I drop matching hunks from the script instead of removing
  // lines here, so display indices stay aligned with the real buffers.
  return out;
}

}  // namespace sdiff_pipeline

REGISTER_COMMAND(
    sdiff,
    /* cmd_name */ "sdiff",
    /* cmd_synopsis */ "sdiff [OPTION]... FILE1 FILE2",
    /* cmd_desc */
    "Side-by-side merge of differences between FILE1 and FILE2.\n",
    /* examples */
    "  sdiff -w 80 file1.txt file2.txt    Two columns, 80 print columns\n"
    "  sdiff -o merged.txt l.txt r.txt    Write auto-merged file\n"
    "  sdiff -s l.txt r.txt               Skip common lines",
    /* see_also */ "diff(1), diff3(1), cmp(1), patch(1)",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ SDIFF_OPTIONS) {
  using namespace sdiff_pipeline;
  using namespace core::pipeline;

#ifdef _WIN32
  // [GNU] "-" reads raw stdin bytes; text mode would strip CR.
  _setmode(_fileno(stdin), _O_BINARY);
#endif

  auto try_help = [](const std::string &message) -> int {
    safeErrorPrint("sdiff: ");
    safeErrorPrintLn(message);
    safeErrorPrint("sdiff: Try 'sdiff --help' for more information.\n");
    return 2;
  };

  auto last_raw_argument = []() -> std::string {
    int argc = 0;
    LPWSTR *argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv_w || argc < 1) return "sdiff";
    std::vector<std::string> tokens;
    for (int i = 1; i < argc; ++i) {
      std::string token = wstring_to_utf8(argv_w[i]);
      if (i == 1 && token == "sdiff") continue;
      tokens.push_back(token);
    }
    LocalFree(argv_w);
    if (tokens.empty()) return "sdiff";
    return tokens.back();
  };

  SdiffConfig cfg;
  cfg.output_file = ctx.get<std::string>("-o", "");
  if (cfg.output_file.empty()) {
    cfg.output_file = ctx.get<std::string>("--output", "");
  }
  cfg.left_column = ctx.has("-l") || ctx.has("--left-column");
  cfg.suppress_common = ctx.has("-s") || ctx.has("--suppress-common-lines");
  cfg.expand_tabs = ctx.has("-t") || ctx.has("--expand-tabs");
  cfg.ignore_case = ctx.has("-i") || ctx.has("--ignore-case");
  cfg.ignore_all_space = ctx.has("-W") || ctx.has("--ignore-all-space");
  cfg.ignore_space_change = ctx.has("-b") || ctx.has("--ignore-space-change");
  cfg.ignore_blank_lines = ctx.has("-B") || ctx.has("--ignore-blank-lines");
  cfg.ignore_trailing_space =
      ctx.has("-Z") || ctx.has("--ignore-trailing-space");
  cfg.ignore_tab_expansion = ctx.has("-E") || ctx.has("--ignore-tab-expansion");
  cfg.strip_trailing_cr = ctx.has("--strip-trailing-cr");
  cfg.ignore_matching = ctx.get<std::string>(
      "-I", ctx.get<std::string>("--ignore-matching-lines", ""));
  (void)ctx.has("-a");
  (void)ctx.has("--text");
  (void)ctx.has("-d");
  (void)ctx.has("--minimal");
  (void)ctx.has("-H");
  (void)ctx.has("--speed-large-files");
  std::string diff_program = ctx.get<std::string>("--diff-program", "");

  if (ctx.has("-w") || ctx.has("--width")) {
    cfg.width = ctx.get<int>("-w", ctx.get<int>("--width", 130));
  }
  if (ctx.has("--tabsize")) {
    cfg.tabsize = ctx.get<int>("--tabsize", 8);
  }

  auto files_result = resolve_files(ctx);
  if (!files_result) {
    const std::string &error = files_result.error();
    if (error.rfind("missing operand", 0) == 0) {
      return try_help("missing operand after '" + last_raw_argument() + "'");
    }
    return try_help(error);
  }
  const auto [file1, file2] = *files_result;

  // [GNU] sdiff hands the operands to diff, whose diagnostics are verbatim
  // ("diff: <path>: ..."); each side is checked in order.
  for (const std::string *path : {&file1, &file2}) {
    if (de::operand_is_stdin(*path)) continue;
    auto operand = native_path::make_api_path_operand(*path);
    if (native_path::operand_target_attributes_w(operand) ==
        INVALID_FILE_ATTRIBUTES) {
      safeErrorPrint("diff: ");
      safeErrorPrint(*path);
      safeErrorPrintLn(": No such file or directory");
      return 2;
    }
  }

  auto in1 = de::read_file_lines_result(file1);
  auto in2 = de::read_file_lines_result(file2);
  if (!in1) {
    safeErrorPrint("diff: ");
    safeErrorPrintLn(in1.error());
    return 2;
  }
  if (!in2) {
    safeErrorPrint("diff: ");
    safeErrorPrintLn(in2.error());
    return 2;
  }

  const std::vector<std::string> lines1 = in1.value().lines;
  const std::vector<std::string> lines2 = in2.value().lines;

  auto cmp1 = normalize_for_compare(lines1, cfg);
  auto cmp2 = normalize_for_compare(lines2, cfg);
  // [GNU io.c] The incomplete-last-line rule, disabled by -Z/-b/-W.
  const bool newline_insensitive = cfg.ignore_trailing_space ||
                                   cfg.ignore_space_change ||
                                   cfg.ignore_all_space;
  diff_engine::mark_incomplete_last_line(cmp1, in1.value().ends_with_newline,
                                         newline_insensitive);
  diff_engine::mark_incomplete_last_line(cmp2, in2.value().ends_with_newline,
                                         newline_insensitive);
  auto edits = de::compute_diff(cmp1, cmp2);
  if (cfg.ignore_blank_lines || !cfg.ignore_matching.empty()) {
    edits = de::drop_ignored_hunks(
        edits, lines1, lines2,
        de::make_blank_or_matching_predicate(cfg.ignore_matching));
  }

  de::SdiffStyle style;
  style.tabsize = cfg.tabsize;
  style.expand_tabs = cfg.expand_tabs;

  // [GNU] The display is exactly `diff --side-by-side` output.
  auto sink = [](std::string_view text) { safePrint(text); };
  de::emit_side_by_side(edits, lines1, lines2, in1.value().ends_with_newline,
                        in2.value().ends_with_newline, cfg.width,
                        cfg.suppress_common, cfg.left_column, style, sink);

  if (cfg.output_file.empty()) {
    if (cfg.ignore_blank_lines || !cfg.ignore_matching.empty()) {
      // [GNU] Ignored hunks do not make the inputs differ.
      const bool real = std::any_of(
          edits.begin(), edits.end(),
          [](const de::Edit &edit) { return edit.type != de::EditType::KEEP; });
      return real ? 1 : 0;
    }
    return edits.empty() ? 0 : 1;
  }

  // -- -o: write the merged file (non-interactive auto-merge, #1127 P1) --
  HANDLE hFile =
      CreateFileW(utf8_to_wstring(cfg.output_file).c_str(), GENERIC_WRITE, 0,
                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    safeErrorPrint("sdiff: ");
    safeErrorPrint(cfg.output_file);
    safeErrorPrintLn(": No such file or directory");
    return 2;
  }

  std::string merged;
  auto merge_line = [&](const std::string &line, bool has_newline) {
    merged += line;
    if (has_newline) merged += "\n";
  };
  auto left_has_nl = [&](size_t idx) {
    return idx + 1 < lines1.size() || in1.value().ends_with_newline;
  };
  auto right_has_nl = [&](size_t idx) {
    return idx + 1 < lines2.size() || in2.value().ends_with_newline;
  };

  size_t k = 0;
  while (k < edits.size()) {
    if (edits[k].type == de::EditType::KEEP) {
      while (k < edits.size() && edits[k].type == de::EditType::KEEP) {
        merge_line(lines1[edits[k].line1_index],
                   left_has_nl(edits[k].line1_index));
        ++k;
      }
      continue;
    }
    std::vector<size_t> dels;
    std::vector<size_t> ins;
    while (k < edits.size() && edits[k].type != de::EditType::KEEP) {
      if (edits[k].type == de::EditType::DEL) {
        dels.push_back(edits[k].line1_index);
      } else {
        ins.push_back(edits[k].line2_index);
      }
      ++k;
    }
    // Auto-merge choice: paired and left-over deleted rows keep the left
    // side, right-over inserted rows take the right side.
    const size_t paired = std::min(dels.size(), ins.size());
    for (size_t row = 0; row < paired; ++row) {
      merge_line(lines1[dels[row]], left_has_nl(dels[row]));
    }
    for (size_t row = paired; row < dels.size(); ++row) {
      merge_line(lines1[dels[row]], left_has_nl(dels[row]));
    }
    for (size_t row = paired; row < ins.size(); ++row) {
      merge_line(lines2[ins[row]], right_has_nl(ins[row]));
    }
  }

  // Positional tail: lines beyond the script merge in lockstep (this also
  // covers identical inputs, where the script is empty).
  size_t last1 = 0;
  size_t last2 = 0;
  for (const auto &edit : edits) {
    if (edit.type != de::EditType::INS) {
      last1 = std::max(last1, edit.line1_index + 1);
    }
    if (edit.type != de::EditType::DEL) {
      last2 = std::max(last2, edit.line2_index + 1);
    }
  }
  size_t t1 = last1;
  size_t t2 = last2;
  while (t1 < lines1.size() && t2 < lines2.size()) {
    merge_line(lines1[t1], left_has_nl(t1));
    ++t1;
    ++t2;
  }
  while (t1 < lines1.size()) {
    merge_line(lines1[t1], left_has_nl(t1));
    ++t1;
  }
  while (t2 < lines2.size()) {
    merge_line(lines2[t2], right_has_nl(t2));
    ++t2;
  }

  DWORD written = 0;
  const BOOL write_ok =
      WriteFile(hFile, merged.data(), static_cast<DWORD>(merged.size()),
                &written, nullptr);
  CloseHandle(hFile);
  if (!write_ok) {
    safeErrorPrint("sdiff: ");
    safeErrorPrint(cfg.output_file);
    safeErrorPrintLn(": write failed");
    return 2;
  }

  // [GNU] A completed merge reports the subsidiary diff's success, not
  // the difference count.
  return 0;
}
