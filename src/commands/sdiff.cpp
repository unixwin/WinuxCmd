// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sdiff command.
///     Side-by-side merge of file differences following GNU diffutils
///     3.10: the display is exactly `diff --side-by-side` output (GNU
///     sdiff execs diff for it), and -o merges interactively with
///     GNU's own protocol (sdiff.c edit loop): every change hunk is
///     shown, a '%' prompt is printed, and a one-letter command is
///     read from stdin - l/r pick a side, s/v toggle whether common
///     lines are displayed, the e* family edits the hunk in $EDITOR,
///     q quits with exit status 2 and the output file left partial.
///     One deliberate deviation (issue #1127 P1): where GNU turns EOF
///     at a prompt into q, WinuxCmd falls back to auto-merging the
///     current and remaining hunks non-interactively (clean changes
///     take their only side, overlapping changes keep the left
///     column) and reports the completed merge's verdict, so -o stays
///     usable from scripts that provide no interaction at all.
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
  de::IgnoreMatchingPatterns ignore_matching;
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

// Positional sections over the raw edit script (SdiffScript /
// build_sdiff_script) live in diff_engine.h: `diff -y` with -B/-I
// renders through the same sections.

// ======================================================
// Interactive merge (GNU sdiff.c interact + edit)
// ======================================================

// The help text GNU prints to stderr for an unrecognized or badly
// terminated merge command (sdiff.c give_help), byte for byte.
inline constexpr std::string_view kInteractHelp =
    "ed:\tEdit then use both versions, each decorated with a header.\n"
    "eb:\tEdit then use both versions.\n"
    "el or e1:\tEdit then use the left version.\n"
    "er or e2:\tEdit then use the right version.\n"
    "e:\tDiscard both versions then edit a new one.\n"
    "l or 1:\tUse the left version.\n"
    "r or 2:\tUse the right version.\n"
    "s:\tSilently include common lines.\n"
    "v:\tVerbosely include common lines.\n"
    "q:\tQuit.\n";

enum class MergeCommand {
  Left,
  Right,
  Suppress,  // 's': stop displaying common lines, ask again
  Verbose,   // 'v': display common lines, ask again
  Quit,
  EditDecorated,  // 'ed'
  EditBoth,       // 'eb'
  EditLeft,       // 'el' / 'e1'
  EditRight,      // 'er' / 'e2'
  EditNew,        // 'e'
  EndOfInput,     // EOF: the caller switches to auto-merge (#1127 P1)
};

// Reads merge commands from stdin with GNU sdiff.c's prompt protocol:
// '%' on stdout before every read, whitespace skipped up to (but not
// past) the newline, the line's remainder required to be blank, and
// the help text on stderr when a command is not recognized.  GNU
// turns EOF into 'q'; the prompter reports it as its own result so
// the caller can apply the recorded auto-merge fallback instead.
class MergePrompter {
 public:
  auto read_command() -> MergeCommand {
    for (;;) {
      safePrint("%");
      const int cmd0 = skip_white();
      if (cmd0 == kEof) return MergeCommand::EndOfInput;
      switch (cmd0) {
        case '1':
        case 'l':
        case '2':
        case 'r':
        case 's':
        case 'v':
        case 'q': {
          if (!expect_blank_rest()) break;
          switch (cmd0) {
            case '1':
            case 'l':
              return MergeCommand::Left;
            case '2':
            case 'r':
              return MergeCommand::Right;
            case 's':
              return MergeCommand::Suppress;
            case 'v':
              return MergeCommand::Verbose;
            default:
              return MergeCommand::Quit;
          }
        }
        case 'e': {
          const int cmd1 = skip_white();
          if (cmd1 == kEof) return MergeCommand::EndOfInput;
          switch (cmd1) {
            case '1':
            case 'l':
            case '2':
            case 'r':
            case 'b':
            case 'd':
              if (!expect_blank_rest()) break;
              switch (cmd1) {
                case '1':
                case 'l':
                  return MergeCommand::EditLeft;
                case '2':
                case 'r':
                  return MergeCommand::EditRight;
                case 'b':
                  return MergeCommand::EditBoth;
                default:
                  return MergeCommand::EditDecorated;
              }
              break;
            case '\n':
              return MergeCommand::EditNew;
            default:
              safeErrorPrint(kInteractHelp);
              flush_line();
              break;
          }
          break;
        }
        case '\n':
          safeErrorPrint(kInteractHelp);
          break;
        default:
          flush_line();
          safeErrorPrint(kInteractHelp);
          break;
      }
    }
  }

 private:
  static constexpr int kEof = -1;

  auto get() -> int {
    const int c = std::cin.get();
    return c == std::char_traits<char>::eof() ? kEof : c;
  }

  // skip_white: consume spaces and tabs (any isspace but '\n') and
  // return the first other character, or kEof at end of input.
  auto skip_white() -> int {
    for (;;) {
      const int c = get();
      if (c == EOF) return kEof;
      if (!std::isspace(static_cast<unsigned char>(c)) || c == '\n') return c;
    }
  }

  void flush_line() {
    int c;
    while ((c = get()) != EOF && c != '\n') {
    }
  }

  // After a command character the rest of the line must be blank;
  // otherwise the help text is printed and the line is discarded.
  auto expect_blank_rest() -> bool {
    if (skip_white() == '\n') return true;
    safeErrorPrint(kInteractHelp);
    flush_line();
    return false;
  }
};

// The 'e' family: compose a temp file with the hunk's text (for 'ed',
// each side decorated with a --- / +++ header naming the file and its
// 1-based line range), hand it to $EDITOR - "ed" when EDITOR is unset,
// exactly like GNU - and use the file's resulting bytes verbatim.  A
// launch failure or a nonzero editor exit is trouble, reported with
// GNU's subsidiary-program diagnostics.
class MergeEditor {
 public:
  MergeEditor() = default;
  MergeEditor(const MergeEditor &) = delete;
  auto operator=(const MergeEditor &) -> MergeEditor & = delete;
  ~MergeEditor() {
    if (temp_created_) DeleteFileW(temp_path_.c_str());
  }

  // Appends the edited bytes for `hunk` to `merged`; returns false on
  // trouble (the diagnostic has already been printed).
  auto edit(MergeCommand command, const de::SdiffSection &hunk,
            const std::vector<std::string> &lines1,
            const std::vector<std::string> &lines2, bool ends_with_newline1,
            bool ends_with_newline2, const std::string &name1,
            const std::string &name2, std::string &merged) -> bool {
    std::string content;
    auto append_span = [&content](const std::vector<std::string> &lines,
                                  size_t begin, size_t end,
                                  bool ends_with_newline) {
      for (size_t i = begin; i < end; ++i) {
        content += lines[i];
        if (i + 1 < lines.size() || ends_with_newline) content += '\n';
      }
    };
    auto header = [](const char *mark, const std::string &name, size_t begin,
                     size_t end) {
      std::string line = mark + name + " " + std::to_string(begin + 1);
      if (end - begin > 1) line += "," + std::to_string(end);
      return line + "\n";
    };
    switch (command) {
      case MergeCommand::EditDecorated:
        if (hunk.left_end > hunk.left_begin) {
          content += header("--- ", name1, hunk.left_begin, hunk.left_end);
        }
        append_span(lines1, hunk.left_begin, hunk.left_end,
                    ends_with_newline1);
        if (hunk.right_end > hunk.right_begin) {
          content += header("+++ ", name2, hunk.right_begin, hunk.right_end);
        }
        append_span(lines2, hunk.right_begin, hunk.right_end,
                    ends_with_newline2);
        break;
      case MergeCommand::EditBoth:
        append_span(lines1, hunk.left_begin, hunk.left_end,
                    ends_with_newline1);
        append_span(lines2, hunk.right_begin, hunk.right_end,
                    ends_with_newline2);
        break;
      case MergeCommand::EditLeft:
        append_span(lines1, hunk.left_begin, hunk.left_end,
                    ends_with_newline1);
        break;
      case MergeCommand::EditRight:
        append_span(lines2, hunk.right_begin, hunk.right_end,
                    ends_with_newline2);
        break;
      default:  // EditNew starts from an empty file
        break;
    }

    if (!ensure_temp_file() || !write_temp(content)) {
      safeErrorPrintLn("sdiff: cannot create temporary file");
      return false;
    }

    const std::string program = editor_program();
    const std::optional<int> status =
        run_editor(program, wstring_to_utf8(temp_path_));
    if (!status) {
      safeErrorPrint("sdiff: subsidiary program '");
      safeErrorPrint(program);
      safeErrorPrintLn("' not found");
      return false;
    }
    if (*status != 0) {
      safeErrorPrint("sdiff: subsidiary program '");
      safeErrorPrint(program);
      safeErrorPrint("' failed (exit status ");
      safeErrorPrintLn(std::to_string(*status) + ")");
      return false;
    }

    std::string edited;
    if (!read_temp(edited)) {
      safeErrorPrintLn("sdiff: cannot read temporary file");
      return false;
    }
    merged += edited;
    return true;
  }

 private:
  std::wstring temp_path_;
  bool temp_created_ = false;

  static auto editor_program() -> std::string {
    if (const char *env = std::getenv("EDITOR")) return env;
    return "ed";
  }

  auto ensure_temp_file() -> bool {
    if (temp_created_) return true;
    std::array<wchar_t, MAX_PATH> dir{};
    if (GetTempPathW(static_cast<DWORD>(dir.size()), dir.data()) == 0) {
      return false;
    }
    std::array<wchar_t, MAX_PATH> path{};
    if (GetTempFileNameW(dir.data(), L"sdiff", 0, path.data()) == 0) {
      return false;
    }
    temp_path_ = path.data();
    temp_created_ = true;
    return true;
  }

  auto write_temp(const std::string &content) -> bool {
    HANDLE handle = CreateFileW(temp_path_.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok =
        content.empty() ||
        (WriteFile(handle, content.data(), static_cast<DWORD>(content.size()),
                   &written, nullptr) &&
         written == content.size());
    CloseHandle(handle);
    return ok;
  }

  auto read_temp(std::string &content) -> bool {
    HANDLE handle = CreateFileW(temp_path_.c_str(), GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    std::array<char, 8192> buffer{};
    DWORD read = 0;
    bool ok = true;
    for (;;) {
      if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &read, nullptr)) {
        ok = false;
        break;
      }
      if (read == 0) break;
      content.append(buffer.data(), read);
    }
    CloseHandle(handle);
    return ok;
  }

  // Spawn the editor with the process's own standard handles, wait
  // for it, and report its exit status (nullopt when it cannot be
  // launched at all).  Resolution follows the same GNU execvp
  // (PATH-only) semantics the diff3 subsidiary diff uses.
  static auto run_editor(const std::string &program, const std::string &file)
      -> std::optional<int> {
    winux::childproc::ChildLaunchRequest request;
    request.command = program;
    request.args.emplace_back(file);
    auto launch = winux::childproc::plan_child_launch(request);
    if (launch.status != winux::childproc::ChildResolveStatus::Found) {
      return std::nullopt;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmdline = launch.command_line;
    const BOOL spawned = CreateProcessW(
        launch.application_name ? launch.application_name->c_str() : nullptr,
        cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    if (!spawned) return std::nullopt;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
  }
};

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
      return try_help("missing operand after '" +
                      de::last_raw_argument("sdiff", ctx.raw_args) + "'");
    }
    return try_help(error);
  }
  const auto [file1, file2] = *files_result;

  // [GNU #1449] Every -I/--ignore-matching-lines occurrence adds one
  // basic regular expression, compiled before the operands are touched
  // (an invalid RE is diagnosed ahead of a missing input file, as GNU
  // does; missing operands themselves are still reported above, by
  // resolve_files).  The diagnostic is diff's, verbatim like the other
  // forwarded diagnostics below.
  {
    std::vector<std::string> ignore_texts;
    for (const auto &occ :
         ctx.string_occurrences({"-I", "--ignore-matching-lines"})) {
      ignore_texts.push_back(std::string(occ.value));
    }
    auto compiled_ignore = de::compile_ignore_matching_patterns(ignore_texts);
    if (!compiled_ignore) {
      safeErrorPrint("diff: invalid regular expression '");
      safeErrorPrint(compiled_ignore.error());
      safeErrorPrint("'\n");
      return 2;
    }
    cfg.ignore_matching = std::move(*compiled_ignore);
  }

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
  const auto edits = de::compute_diff(cmp1, cmp2);

  // Positional sections over the raw script, with the -B/-I ignore
  // predicate applied per hunk (GNU analyze.c): ignored hunks are not
  // changes, and their lines rejoin the common spans around them.
  std::function<bool(const std::vector<de::Edit> &,
                     const std::vector<std::string> &,
                     const std::vector<std::string> &, size_t, size_t)>
      hunk_is_ignored;
  if (cfg.ignore_blank_lines || !cfg.ignore_matching.empty()) {
    hunk_is_ignored = de::make_blank_or_matching_predicate(
        cfg.ignore_blank_lines, cfg.ignore_matching);
  }
  const de::SdiffScript script =
      de::build_sdiff_script(edits, lines1, lines2, hunk_is_ignored);

  // [GNU --strip-trailing-cr] The CR is stripped when the input is
  // read (io.c), so it never reaches the display; the rest of the
  // ignore family normalizes comparison copies only.  The merged
  // output still copies the original lines, like the line filters
  // GNU sdiff reads its inputs through when it merges.
  const std::vector<std::string> shown1 =
      cfg.strip_trailing_cr ? de::strip_trailing_cr_lines(lines1) : lines1;
  const std::vector<std::string> shown2 =
      cfg.strip_trailing_cr ? de::strip_trailing_cr_lines(lines2) : lines2;

  de::SdiffStyle style;
  style.tabsize = cfg.tabsize;
  style.expand_tabs = cfg.expand_tabs;
  const de::SdiffColumns cols =
      de::sdiff_columns(cfg.width, style.tabsize, style.expand_tabs);
  const bool eol1 = in1.value().ends_with_newline;
  const bool eol2 = in2.value().ends_with_newline;
  auto append_rows = [&](const de::SdiffSection &section, std::string &out) {
    de::append_sdiff_section_rows(section, shown1, shown2, eol1, eol2, cols,
                                  style, cfg.left_column, out);
  };

  if (cfg.output_file.empty()) {
    // [GNU] Without -o the display is exactly `diff --side-by-side`.
    std::string out;
    for (const auto &section : script.sections) {
      if (!section.is_hunk && cfg.suppress_common) continue;
      append_rows(section, out);
      if (!out.empty()) {
        safePrint(out);
        out.clear();
      }
    }
    // [GNU] Ignored hunks do not make the inputs differ.
    return script.differ ? 1 : 0;
  }

  // -- -o: interactive merge (GNU sdiff.c interact + edit) --
  if (de::operand_is_stdin(file1) || de::operand_is_stdin(file2)) {
    safeErrorPrintLn("sdiff: cannot interactively merge standard input");
    return 2;
  }

  // [GNU] The output file is created before the first prompt, and the
  // merged lines are written as the walk proceeds, so quitting leaves
  // exactly the prefix merged so far.
  HANDLE hFile =
      CreateFileW(utf8_to_wstring(cfg.output_file).c_str(), GENERIC_WRITE, 0,
                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    safeErrorPrint("sdiff: ");
    safeErrorPrint(cfg.output_file);
    safeErrorPrintLn(": No such file or directory");
    return 2;
  }

  auto span_has_nl = [](const std::vector<std::string> &lines,
                        bool ends_with_newline, size_t idx) {
    return idx + 1 < lines.size() || ends_with_newline;
  };
  std::string pending;
  bool write_failed = false;
  auto flush_pending = [&]() {
    if (pending.empty()) return;
    DWORD written = 0;
    if (!WriteFile(hFile, pending.data(),
                   static_cast<DWORD>(pending.size()), &written, nullptr) ||
        written != pending.size()) {
      write_failed = true;
    }
    pending.clear();
  };
  auto append_span = [&](const std::vector<std::string> &lines,
                         bool ends_with_newline, size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      pending += lines[i];
      if (span_has_nl(lines, ends_with_newline, i)) pending += '\n';
    }
  };
  // The recorded auto-merge (issue #1127 P1), used when stdin reaches
  // EOF: paired and left-over deleted rows keep the left side,
  // right-over inserted rows take the right side.
  auto append_auto_merge = [&](const de::SdiffSection &hunk) {
    append_span(lines1, eol1, hunk.left_begin, hunk.left_end);
    const size_t paired =
        std::min(hunk.left_end - hunk.left_begin,
                 hunk.right_end - hunk.right_begin);
    append_span(lines2, eol2, hunk.right_begin + paired, hunk.right_end);
  };

  MergePrompter prompter;
  MergeEditor editor;
  bool interactive = true;
  bool suppress = cfg.suppress_common;
  bool aborted = false;

  for (const auto &section : script.sections) {
    std::string rows;
    if (!section.is_hunk) {
      if (!suppress) append_rows(section, rows);
      if (!rows.empty()) safePrint(rows);
      // Common lines always reach the merged file from the left side;
      // the right span (including any ')'-only lines) is skipped.
      append_span(lines1, eol1, section.left_begin, section.left_end);
      flush_pending();
      continue;
    }
    append_rows(section, rows);
    if (!rows.empty()) safePrint(rows);
    if (!interactive) {
      append_auto_merge(section);
      flush_pending();
      continue;
    }
    bool resolved = false;
    while (!resolved) {
      const MergeCommand command = prompter.read_command();
      switch (command) {
        case MergeCommand::Left:
          append_span(lines1, eol1, section.left_begin, section.left_end);
          resolved = true;
          break;
        case MergeCommand::Right:
          append_span(lines2, eol2, section.right_begin, section.right_end);
          resolved = true;
          break;
        case MergeCommand::Suppress:
          suppress = true;
          break;
        case MergeCommand::Verbose:
          suppress = false;
          break;
        case MergeCommand::Quit:
          aborted = true;
          resolved = true;
          break;
        case MergeCommand::EndOfInput:
          interactive = false;
          append_auto_merge(section);
          resolved = true;
          break;
        case MergeCommand::EditDecorated:
        case MergeCommand::EditBoth:
        case MergeCommand::EditLeft:
        case MergeCommand::EditRight:
        case MergeCommand::EditNew: {
          std::string edited;
          if (!editor.edit(command, section, lines1, lines2, eol1, eol2,
                           file1, file2, edited)) {
            CloseHandle(hFile);
            return 2;
          }
          pending += edited;
          resolved = true;
          break;
        }
      }
    }
    if (aborted) break;
    flush_pending();
  }

  CloseHandle(hFile);
  if (write_failed) {
    safeErrorPrint("sdiff: ");
    safeErrorPrint(cfg.output_file);
    safeErrorPrintLn(": write failed");
    return 2;
  }
  if (aborted) return 2;
  // [GNU] A completed merge reports the subsidiary diff's verdict: 1
  // when the inputs differ (after -B/-I filtering), 0 when they are
  // the same (#1441).
  return script.differ ? 1 : 0;
}
