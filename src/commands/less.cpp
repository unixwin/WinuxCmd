// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for less.
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

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr LESS_OPTIONS = std::array{
    // [EXT]
    OPTION("-e", "--quit-at-eof", "quit after second EOF", BOOL_TYPE),
    // [EXT]
    OPTION("-E", "--QUIT-AT-EOF", "quit after first EOF", BOOL_TYPE),
    // [EXT]
    OPTION("-F", "--quit-if-one-screen",
           "quit if entire file fits on first screen", BOOL_TYPE),
    // [EXT]
    OPTION("-i", "--ignore-case",
           "ignore case in searches that do not contain uppercase", BOOL_TYPE),
    // [EXT]
    OPTION("-I", "--IGNORE-CASE", "ignore case in all searches", BOOL_TYPE),
    // [EXT]
    OPTION("-K", "--quit-on-intr", "exit less in response to Ctrl+C",
           BOOL_TYPE),
    // [EXT]
    OPTION("-n", "--line-numbers", "display line number at start of each line",
           BOOL_TYPE),
    // [EXT]
    OPTION("-N", "--LINE-NUMBERS", "display line number at start of each line",
           BOOL_TYPE),
    // [EXT]
    OPTION("-S", "--chop-long-lines", "chop long lines (do not wrap)",
           BOOL_TYPE),
    // [EXT]
    OPTION("-q", "--quiet", "accepted placeholder; terminal bell is not used",
           BOOL_TYPE),
    // [EXT]
    OPTION("-Q", "--QUIET", "accepted placeholder; terminal bell is not used",
           BOOL_TYPE),
    // [EXT]
    OPTION("-r", "--raw-control-chars",
           "accepted placeholder; control chars are passed through", BOOL_TYPE),
    // [EXT]
    OPTION("-R", "--RAW-CONTROL-CHARS",
           "accepted placeholder; ANSI SGR color sequences are passed through",
           BOOL_TYPE),
    // [EXT]
    OPTION("", "--no-init",
           "accepted placeholder; terminal init/deinit sequences are not used",
           BOOL_TYPE),
    // [EXT]
    OPTION("-z", "--window", "set scrolling window size", INT_TYPE),
    // [EXT]
    OPTION("-NUM", "", "same as -z NUM", INT_TYPE),
    // [GNU] --file-size: display the file size in the prompt
    OPTION("", "--file-size", "display the size of the file in the prompt",
           BOOL_TYPE),
    // [GNU] --follow-name: follow the file by name in follow mode (F)
    OPTION("", "--follow-name", "follow the file by name in follow mode",
           BOOL_TYPE),
    // [GNU] --incsearch: search incrementally as the pattern is typed
    OPTION("", "--incsearch", "incremental search", BOOL_TYPE),
    // [GNU] --line-num-width: width of the line number field
    OPTION("", "--line-num-width", "set the width of the line number field",
           INT_TYPE),
    // [GNU] --mouse: enable mouse wheel scrolling
    OPTION("", "--mouse", "enable mouse support (wheel scrolling)", BOOL_TYPE),
    // [GNU] --no-histdups: keep no duplicates in the search history
    OPTION("", "--no-histdups",
           "do not keep duplicate entries in the search history", BOOL_TYPE),
    // [GNU] --no-keypad: do not initialize the terminal keypad
    OPTION("", "--no-keypad",
           "accepted placeholder; no keypad init strings are sent", BOOL_TYPE),
    // [GNU] --rscroll: character marking a chopped line (default '>')
    OPTION("", "--rscroll", "set the character used to mark chopped lines",
           STRING_TYPE)};

namespace less_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool quit_at_eof = false;        // -e
  bool quit_first_eof = false;     // -E
  bool quit_one_screen = false;    // -F
  bool ignore_case = false;        // -i
  bool ignore_case_all = false;    // -I
  bool quit_on_intr = false;       // -K
  bool show_line_numbers = false;  // -n, -N
  bool chop_long_lines = false;    // -S
  bool quiet = false;              // -q accepted; pager does not ring a bell.
  bool never_bell = false;         // -Q accepted; pager does not ring a bell.
  bool raw_control = false;        // -r accepted; pass-through output mode.
  bool raw_control_color = false;  // -R accepted; ANSI SGR is passed through.
  bool no_init = false;            // --no-init accepted; no alternate screen.
  int window_size = -1;            // -z, -NUM
  bool file_size = false;          // --file-size: byte counts in the prompt
  bool follow_name = false;        // --follow-name
  bool incsearch = false;          // --incsearch
  int line_num_width = 6;          // --line-num-width
  bool mouse = false;              // --mouse
  bool no_histdups = false;        // --no-histdups
  bool no_keypad = false;          // --no-keypad accepted; nothing to init.
  std::string rscroll = ">";       // --rscroll
  bool default_stdin = false;
  SmallVector<std::string, 16> files;
};

auto build_config(const CommandContext<LESS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.quit_at_eof =
      ctx.get<bool>("--quit-at-eof", false) || ctx.get<bool>("-e", false);
  cfg.quit_first_eof =
      ctx.get<bool>("--QUIT-AT-EOF", false) || ctx.get<bool>("-E", false);
  cfg.quit_one_screen = ctx.get<bool>("--quit-if-one-screen", false) ||
                        ctx.get<bool>("-F", false);
  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-i", false);
  cfg.ignore_case_all =
      ctx.get<bool>("--IGNORE-CASE", false) || ctx.get<bool>("-I", false);
  cfg.quit_on_intr =
      ctx.get<bool>("--quit-on-intr", false) || ctx.get<bool>("-K", false);
  cfg.show_line_numbers =
      ctx.get<bool>("--line-numbers", false) || ctx.get<bool>("-n", false) ||
      ctx.get<bool>("--LINE-NUMBERS", false) || ctx.get<bool>("-N", false);
  cfg.chop_long_lines =
      ctx.get<bool>("--chop-long-lines", false) || ctx.get<bool>("-S", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);
  cfg.never_bell =
      ctx.get<bool>("--QUIET", false) || ctx.get<bool>("-Q", false);
  cfg.raw_control =
      ctx.get<bool>("--raw-control-chars", false) || ctx.get<bool>("-r", false);
  cfg.raw_control_color =
      ctx.get<bool>("--RAW-CONTROL-CHARS", false) || ctx.get<bool>("-R", false);
  cfg.no_init = ctx.get<bool>("--no-init", false);
  cfg.file_size = ctx.get<bool>("--file-size", false);
  cfg.follow_name = ctx.get<bool>("--follow-name", false);
  cfg.incsearch = ctx.get<bool>("--incsearch", false);
  cfg.line_num_width = ctx.get<int>("--line-num-width", 6);
  if (cfg.line_num_width < 0) {
    return std::unexpected("invalid line number width");
  }
  cfg.mouse = ctx.get<bool>("--mouse", false);
  cfg.no_histdups = ctx.get<bool>("--no-histdups", false);
  cfg.no_keypad = ctx.get<bool>("--no-keypad", false);
  if (ctx.has("--rscroll")) {
    cfg.rscroll = ctx.get<std::string>("--rscroll", ">");
  }

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= LESS_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    const auto* value = std::get_if<int>(&occurrence.value);
    if (!value) continue;
    if (option_matches(meta, "-z", "--window") ||
        option_matches(meta, "-NUM", "")) {
      cfg.window_size = *value;
    }
  }

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    cfg.files.push_back(file_arg);
  }

  if (cfg.files.empty()) {
    cfg.default_stdin = true;
    cfg.files.push_back("-");
  }

  return cfg;
}

auto read_file_content(const std::string& filename) -> cp::Result<std::string> {
  std::string content;

  if (filename == "-" || filename.empty()) {
    file_io::set_stdin_binary();
    // Read from stdin
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      return std::unexpected("error reading from standard input");
    }
  } else {
    // Read from file
    // #1385: the file operand is an argv operand: open it through the shared
    // operand boundary so a shell-namespace spelling names the payload root,
    // not the current drive root. Errors report the operand as typed.
    std::ifstream file(native_path::normalize_api_operand(filename),
                       std::ios::binary);
    if (!file) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }
    content.assign(std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>());
    if (file.fail() && !file.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  return content;
}

using PagerDocument = winux::pager::Document;

auto load_interactive_document(const std::string& filename)
    -> std::expected<PagerDocument, std::string> {
  if (filename == "-" || filename.empty()) {
    auto content = read_file_content(filename);
    if (!content) return std::unexpected(std::string(content.error()));
    return winux::pager::load_memory_document("stdin", std::move(*content));
  }

  return winux::pager::load_seekable_document(filename);
}

auto console_size() -> std::pair<int, int> { return getConsoleViewportSize(); }

auto clear_console() -> void { clearConsoleViewport(); }

enum class PagerAction {
  None,
  NextPage,
  NextLine,
  NextHalfPage,
  PrevPage,
  PrevLine,
  PrevHalfPage,
  FirstLine,
  LastLine,
  GoToLine,
  GoToPercent,
  NextFile,
  PrevFile,
  SearchForward,
  SearchBackward,
  RepeatSearch,
  ReverseSearch,
  Repaint,
  Status,
  Follow,
  WheelUp,
  WheelDown,
  Quit
};

struct PagerCommand {
  PagerAction action = PagerAction::None;
  std::string text;
  size_t number = 0;
};

struct SearchState {
  std::string text;
  portable_regex::Pattern pattern;
  bool forward = true;
};

struct MatchLocation {
  size_t line = 0;
  size_t begin = 0;
  size_t end = 0;
};

// Defined further below; forward-declared for the incremental-search
// preview used while a search pattern is being typed.
auto compile_search(const Config& cfg, std::string_view text, bool forward)
    -> std::optional<SearchState>;
auto find_search_match(const PagerDocument& doc, const SearchState& search,
                       size_t anchor_line, bool forward)
    -> std::optional<MatchLocation>;

auto redraw_search_text(wchar_t prompt, std::wstring_view text,
                        std::wstring_view suffix = {}) -> void {
  safePrint("\r\033[K");
  safePrint(std::wstring_view(&prompt, 1));
  safePrint(text);
  if (!suffix.empty()) safePrint(suffix);
}

auto push_search_history(std::vector<std::wstring>& history,
                         std::wstring_view text, bool no_histdups) -> void {
  if (text.empty()) return;
  if (!history.empty() && history.back() == text) return;
  // [GNU] --no-histdups: an older identical entry is removed, not just
  // a duplicate of the most recent one.
  if (no_histdups) {
    std::erase_if(history,
                  [&](const std::wstring& entry) { return entry == text; });
  }
  history.emplace_back(text);
}

[[nodiscard]] auto is_interrupt_key(const KEY_EVENT_RECORD& key) -> bool {
  return key.uChar.AsciiChar == 3 || key.wVirtualKeyCode == VK_CANCEL;
}

struct PromptResult {
  PagerAction action = PagerAction::None;
  std::string text;
};

// [GNU] --incsearch preview: called with the pattern text as it is
// being typed; returns a status suffix (for example " [line 12]") that
// is shown after the pattern. Empty when incremental search is off.
using SearchPreview = std::function<std::wstring(std::wstring_view)>;

auto read_search_text(HANDLE input, wchar_t prompt, bool quit_on_intr,
                      std::vector<std::wstring>& history, bool no_histdups,
                      const SearchPreview& preview) -> PromptResult {
  std::wstring text;
  std::optional<std::wstring> draft;
  size_t history_index = history.size();
  safePrint("\r\033[K");
  safePrint(std::wstring_view(&prompt, 1));

  auto redraw = [&]() {
    redraw_search_text(prompt, text, preview ? preview(text) : std::wstring{});
  };

  INPUT_RECORD record{};
  DWORD read = 0;
  while (ReadConsoleInputW(input, &record, 1, &read)) {
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
      continue;
    }

    const auto& key = record.Event.KeyEvent;
    if (quit_on_intr && is_interrupt_key(key)) return {PagerAction::Quit, {}};
    if (key.wVirtualKeyCode == VK_ESCAPE) return {PagerAction::None, {}};
    if (key.wVirtualKeyCode == VK_RETURN) {
      safePrint("\n");
      push_search_history(history, text, no_histdups);
      return {PagerAction::None, wstring_to_utf8(text)};
    }
    if (key.wVirtualKeyCode == VK_UP || key.wVirtualKeyCode == VK_DOWN) {
      if (history.empty()) continue;

      if (!draft && history_index == history.size()) {
        draft = text;
      }

      if (key.wVirtualKeyCode == VK_UP) {
        if (history_index > 0) --history_index;
      } else if (history_index < history.size()) {
        ++history_index;
      }

      text = history_index < history.size() ? history[history_index]
                                            : draft.value_or(std::wstring{});
      redraw();
      continue;
    }
    if (key.wVirtualKeyCode == VK_BACK) {
      if (!text.empty()) {
        text.pop_back();
        history_index = history.size();
        draft.reset();
        redraw();
      }
      continue;
    }

    wchar_t ch = key.uChar.UnicodeChar;
    if (ch >= L' ') {
      text.push_back(ch);
      history_index = history.size();
      draft.reset();
      if (preview) {
        redraw();
      } else {
        safePrint(std::wstring_view(&ch, 1));
      }
    }
  }
  return {PagerAction::Quit, {}};
}

auto read_colon_command(HANDLE input, bool quit_on_intr, size_t number = 0)
    -> PagerCommand {
  std::wstring text;
  safePrint("\r\033[K");
  safePrint(":");

  INPUT_RECORD record{};
  DWORD read = 0;
  while (ReadConsoleInputW(input, &record, 1, &read)) {
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
      continue;
    }

    const auto& key = record.Event.KeyEvent;
    if (quit_on_intr && is_interrupt_key(key)) return {PagerAction::Quit, {}};
    if (key.wVirtualKeyCode == VK_ESCAPE) return {PagerAction::None, {}};
    if (key.wVirtualKeyCode == VK_RETURN) {
      safePrint("\n");
      std::string command = wstring_to_utf8(text);
      if (command == "n") return {PagerAction::NextFile, {}, number};
      if (command == "p") return {PagerAction::PrevFile, {}, number};
      if (command == "f") return {PagerAction::Status, {}};
      return {PagerAction::None, {}};
    }
    if (key.wVirtualKeyCode == VK_BACK) {
      if (!text.empty()) {
        text.pop_back();
        safePrint("\r\033[K:");
        safePrint(text);
      }
      continue;
    }

    wchar_t ch = key.uChar.UnicodeChar;
    if (ch >= L' ') {
      text.push_back(ch);
      safePrint(std::wstring_view(&ch, 1));
    }
  }

  return {PagerAction::Quit, {}};
}

auto read_number_command(HANDLE input, char first_digit, bool quit_on_intr)
    -> PagerCommand {
  std::string digits(1, first_digit);

  INPUT_RECORD record{};
  DWORD read = 0;
  while (ReadConsoleInputW(input, &record, 1, &read)) {
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
      continue;
    }

    const auto& key = record.Event.KeyEvent;
    if (quit_on_intr && is_interrupt_key(key)) return {PagerAction::Quit, {}};
    char ch = key.uChar.AsciiChar;
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      digits.push_back(ch);
      continue;
    }

    size_t value = 0;
    auto [ptr, ec] =
        std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc() || ptr != digits.data() + digits.size()) {
      return {PagerAction::None, {}};
    }

    if (ch == 'g' || key.wVirtualKeyCode == VK_RETURN) {
      return {PagerAction::GoToLine, {}, value};
    }
    if (ch == '%') {
      return {PagerAction::GoToPercent, {}, value};
    }
    if (ch == 'G') {
      return {PagerAction::GoToPercent, {}, 100};
    }
    if (ch == ':') {
      return read_colon_command(input, quit_on_intr, value);
    }
    return {PagerAction::None, {}};
  }

  return {PagerAction::Quit, {}};
}

auto read_pager_command(HANDLE input, bool quit_on_intr,
                        std::vector<std::wstring>& search_history,
                        const Config& cfg, const PagerDocument* doc,
                        size_t anchor_line) -> PagerCommand {
  INPUT_RECORD record{};
  DWORD read = 0;
  while (ReadConsoleInputW(input, &record, 1, &read)) {
    // [GNU] --mouse: wheel notches scroll three lines.
    if (record.EventType == MOUSE_EVENT && cfg.mouse) {
      const auto& mouse = record.Event.MouseEvent;
      if ((mouse.dwEventFlags & MOUSE_WHEELED) != 0) {
        const short delta = static_cast<short>(mouse.dwButtonState >> 16);
        return {
            delta > 0 ? PagerAction::WheelUp : PagerAction::WheelDown, {}, 3};
      }
      continue;
    }
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
      continue;
    }

    const auto& key = record.Event.KeyEvent;
    if (quit_on_intr && is_interrupt_key(key)) return {PagerAction::Quit, {}};
    char ch = key.uChar.AsciiChar;
    if (ch == 'q' || ch == 'Q') return {PagerAction::Quit, {}};
    if (ch == 'F') return {PagerAction::Follow, {}};
    if (ch == 'b' || ch == 'B' || ch == 2) {
      return {PagerAction::PrevPage, {}};
    }
    if (ch == ' ' || ch == 'f' || ch == 6 || ch == 22) {
      return {PagerAction::NextPage, {}};
    }
    if (ch == '\r' || ch == '\n' || ch == 'e' || ch == 'j' || ch == 5 ||
        ch == 14) {
      return {PagerAction::NextLine, {}};
    }
    if (ch == 'k' || ch == 'y' || ch == 25 || ch == 16) {
      return {PagerAction::PrevLine, {}};
    }
    if (ch == 'd' || ch == 4) return {PagerAction::NextHalfPage, {}};
    if (ch == 'u' || ch == 21) return {PagerAction::PrevHalfPage, {}};
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      return read_number_command(input, ch, quit_on_intr);
    }
    if (ch == 'g' || ch == '<') return {PagerAction::FirstLine, {}};
    if (ch == 'G' || ch == '>') return {PagerAction::LastLine, {}};
    if (ch == 'p' || ch == '%') return {PagerAction::GoToPercent, {}, 0};
    if (ch == 'r' || ch == 'R' || ch == 12 || ch == 18) {
      return {PagerAction::Repaint, {}};
    }
    if (ch == '=' || ch == 7) return {PagerAction::Status, {}};
    if (ch == 'n') return {PagerAction::RepeatSearch, {}};
    if (ch == 'N') return {PagerAction::ReverseSearch, {}};
    if (ch == ':') return read_colon_command(input, quit_on_intr);
    if (ch == '/' || ch == '?') {
      const bool forward = ch == '/';
      // [GNU] --incsearch: as the pattern is typed, show where its
      // first match from the current screen anchor would land. (The
      // full screen-jump preview of GNU less is not reproduced; the
      // prompt reports the target line or that there is no match.)
      SearchPreview preview;
      if (cfg.incsearch && doc != nullptr) {
        preview = [&cfg, doc, anchor_line,
                   forward](std::wstring_view text) -> std::wstring {
          if (text.empty()) return {};
          auto compiled =
              compile_search(cfg, wstring_to_utf8(std::wstring(text)), forward);
          if (!compiled) return L" [invalid pattern]";
          auto match = find_search_match(*doc, *compiled, anchor_line, forward);
          if (!match) return L" [no match]";
          return L" [line " + std::to_wstring(match->line + 1) + L"]";
        };
      }
      auto result = read_search_text(input, forward ? L'/' : L'?', quit_on_intr,
                                     search_history, cfg.no_histdups, preview);
      return result.action == PagerAction::Quit
                 ? PagerCommand{PagerAction::Quit, {}}
                 : PagerCommand{forward ? PagerAction::SearchForward
                                        : PagerAction::SearchBackward,
                                result.text};
    }

    switch (key.wVirtualKeyCode) {
      case VK_HOME:
        return {PagerAction::FirstLine, {}};
      case VK_END:
        return {PagerAction::LastLine, {}};
      case VK_NEXT:
        return {PagerAction::NextPage, {}};
      case VK_PRIOR:
        return {PagerAction::PrevPage, {}};
      case VK_DOWN:
        return {PagerAction::NextLine, {}};
      case VK_UP:
        return {PagerAction::PrevLine, {}};
      case VK_ESCAPE:
        return {PagerAction::Quit, {}};
      default:
        return {PagerAction::None, {}};
    }
  }
  return {PagerAction::Quit, {}};
}

auto compile_search(const Config& cfg, std::string_view text, bool forward)
    -> std::optional<SearchState> {
  if (text.empty()) return std::nullopt;
  bool has_upper = std::ranges::any_of(
      text, [](unsigned char ch) { return std::isupper(ch) != 0; });
  bool ignore_case = cfg.ignore_case_all || (cfg.ignore_case && !has_upper);
  auto compiled = portable_regex::compile(portable_regex::Syntax::Extended,
                                          text, ignore_case);
  if (!compiled) return std::nullopt;
  return SearchState{std::string(text), std::move(compiled.pattern), forward};
}

auto find_search_match(const PagerDocument& doc, const SearchState& search,
                       size_t anchor_line, bool forward)
    -> std::optional<MatchLocation> {
  if (doc.line_count() == 0) return std::nullopt;

  auto line_match = [&](size_t index) -> std::optional<MatchLocation> {
    auto line = doc.line_at(index);
    if (!line) return std::nullopt;
    auto match = search.pattern.find_first(*line);
    if (!match) return std::nullopt;
    return MatchLocation{index, match->begin, match->end};
  };

  if (forward) {
    size_t start = std::min(anchor_line + 1, doc.line_count());
    for (size_t i = start; i < doc.line_count(); ++i) {
      if (auto match = line_match(i)) return match;
    }
    for (size_t i = 0; i < start; ++i) {
      if (auto match = line_match(i)) return match;
    }
  } else {
    size_t start = anchor_line == 0 ? doc.line_count() - 1 : anchor_line - 1;
    for (size_t i = start + 1; i-- > 0;) {
      if (auto match = line_match(i)) return match;
      if (i == 0) break;
    }
    for (size_t i = doc.line_count(); i-- > start + 1;) {
      if (auto match = line_match(i)) return match;
    }
  }
  return std::nullopt;
}

auto highlight_matches(std::string_view text, const SearchState* search,
                       std::optional<std::pair<size_t, size_t>> current_range)
    -> std::string {
  if (!search || !shouldUseAnsiColorStdout()) return std::string(text);

  std::string out;
  size_t cursor = 0;
  while (cursor < text.size()) {
    auto match = search->pattern.find_first(text, cursor);
    if (!match) {
      out.append(text.substr(cursor));
      break;
    }
    if (match->end == match->begin) {
      out.append(text.substr(cursor, match->begin - cursor + 1));
      cursor = match->begin + 1;
      continue;
    }

    out.append(text.substr(cursor, match->begin - cursor));
    const bool is_current = current_range &&
                            current_range->first == match->begin &&
                            current_range->second == match->end;
    out += is_current ? "\033[1;30;43m" : "\033[7m";
    out.append(text.substr(match->begin, match->end - match->begin));
    out += ANSI_RESET;
    cursor = match->end;
  }
  return out;
}

auto print_pager_line(const Config& cfg, size_t line_index,
                      std::string_view line, int width,
                      const SearchState* search,
                      const MatchLocation* current_match) -> void {
  std::string rendered;
  size_t content_offset = 0;
  if (cfg.show_line_numbers) {
    // [GNU] --line-num-width sets the width of the number field
    // (default 6).
    auto prefix = std::format("{:>{}}\t", line_index + 1,
                              static_cast<size_t>(cfg.line_num_width));
    content_offset = prefix.size();
    rendered += prefix;
  }
  rendered.append(line);

  std::optional<std::pair<size_t, size_t>> current_range;
  if (current_match && current_match->line == line_index) {
    current_range = std::pair{current_match->begin + content_offset,
                              current_match->end + content_offset};
  }

  if (cfg.chop_long_lines && width > 0 &&
      rendered.size() > static_cast<size_t>(width)) {
    rendered.resize(static_cast<size_t>(width));
    // [GNU] --rscroll: when a line is chopped, its last visible column
    // shows the rscroll character (default '>'); an empty string
    // disables the marker.
    if (!cfg.rscroll.empty()) {
      rendered.back() = cfg.rscroll.front();
    }
    if (current_range &&
        current_range->first >= static_cast<size_t>(rendered.size())) {
      current_range.reset();
    } else if (current_range) {
      current_range->second =
          std::min(current_range->second, static_cast<size_t>(rendered.size()));
    }
  }

  safePrintLn(highlight_matches(rendered, search, current_range));
}

auto render_page(const Config& cfg, const PagerDocument& doc, size_t top_line,
                 int page_size, int width, const SearchState* search,
                 const MatchLocation* current_match) -> void {
  clear_console();
  for (int i = 0; i < page_size; ++i) {
    size_t line_index = top_line + static_cast<size_t>(i);
    if (line_index >= doc.line_count()) {
      safePrintLn("");
      continue;
    }
    auto line = doc.line_at(line_index);
    if (!line) {
      safePrintLn(line.error());
      continue;
    }
    print_pager_line(cfg, line_index, *line, width, search, current_match);
  }
}

auto print_prompt(const Config& cfg, const PagerDocument& doc,
                  size_t file_index, size_t file_count, size_t top_line,
                  size_t total_lines, size_t page_size, bool at_eof) -> void {
  safePrint(":");
  if (at_eof) {
    safePrint("(END) ");
  }
  safePrint(doc.name);
  if (file_count > 1) {
    safePrint(" [");
    safePrint(std::to_string(file_index + 1));
    safePrint("/");
    safePrint(std::to_string(file_count));
    safePrint("]");
  }
  safePrint("  ");
  size_t top_display = total_lines == 0 ? 0 : top_line + 1;
  size_t bottom_line =
      total_lines == 0 ? 0 : std::min(total_lines, top_line + page_size);
  safePrint("lines ");
  safePrint(std::to_string(top_display));
  safePrint("-");
  safePrint(std::to_string(bottom_line));
  safePrint("/");
  safePrint(std::to_string(total_lines));
  if (cfg.file_size) {
    // [GNU] --file-size: also show the byte position of the end of
    // the visible window against the total size.
    const std::uint64_t total =
        doc.seekable ? doc.file_size : doc.content.size();
    std::uint64_t shown = total;
    if (doc.seekable) {
      if (bottom_line < doc.line_offsets.size()) {
        shown = doc.line_offsets[bottom_line];
      }
    } else {
      std::uint64_t sum = 0;
      for (size_t i = 0; i < bottom_line && i < doc.memory_lines.size(); ++i) {
        sum += doc.memory_lines[i].size() + 1;
      }
      shown = std::min(sum, total);
    }
    safePrint("  bytes ");
    safePrint(std::to_string(shown));
    safePrint("/");
    safePrint(std::to_string(total));
  }
  safePrint("  SPACE:next  b:back  /:search  :n/:p  g/G  q");
}

auto page_size_for(const Config& cfg, int height) -> int {
  int visible_rows = std::max(height - 1, 1);
  if (cfg.window_size <= 0) return visible_rows;
  return std::clamp(cfg.window_size, 1, visible_rows);
}

auto top_line_for_match(size_t match_line, size_t max_top, int page_size)
    -> size_t {
  size_t context = page_size > 4 ? static_cast<size_t>(page_size / 3) : 0;
  size_t target = match_line > context ? match_line - context : 0;
  return std::min(target, max_top);
}

struct PagerResult {
  int code = 0;
  PagerAction action = PagerAction::Quit;
  size_t count = 0;
};

// Identity + size snapshot of a file on disk, for follow mode (F):
// the volume serial and file index identify the file object, so a
// replaced file is distinguishable from a grown one.
struct FileStamp {
  DWORD volume = 0;
  ULONGLONG index = 0;
  std::uint64_t size = 0;
};

auto file_stamp(const std::wstring& wpath) -> std::optional<FileStamp> {
  HANDLE raw =
      CreateFileW(wpath.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (raw == INVALID_HANDLE_VALUE) return std::nullopt;
  UniqueHandle handle(raw);
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(handle.get(), &info)) {
    return std::nullopt;
  }
  ULARGE_INTEGER index{};
  index.HighPart = info.nFileIndexHigh;
  index.LowPart = info.nFileIndexLow;
  LARGE_INTEGER size{};
  size.HighPart = info.nFileSizeHigh;
  size.LowPart = info.nFileSizeLow;
  return FileStamp{info.dwVolumeSerialNumber, index.QuadPart,
                   static_cast<std::uint64_t>(size.QuadPart)};
}

auto simple_pager(const Config& cfg, PagerDocument& doc,
                  const std::string& filename, size_t file_index,
                  size_t file_count) -> PagerResult {
  auto [width, height] = console_size();
  int page_size = page_size_for(cfg, height);

  if (cfg.quit_one_screen &&
      doc.line_count() <= static_cast<size_t>(page_size)) {
    for (size_t i = 0; i < doc.line_count(); ++i) {
      auto line = doc.line_at(i);
      if (!line) return {1, PagerAction::Quit};
      print_pager_line(cfg, i, *line, width, nullptr, nullptr);
    }
    return {0, PagerAction::Quit};
  }

  winux::pager::ConsoleInputMode input_mode(!cfg.quit_on_intr);
  if (!input_mode.active()) {
    for (size_t i = 0; i < doc.line_count(); ++i) {
      auto line = doc.line_at(i);
      if (!line) return {1, PagerAction::Quit};
      safePrintLn(*line);
    }
    return {0, PagerAction::Quit};
  }

  // [GNU] --mouse: ask the console for mouse input (wheel events);
  // ConsoleInputMode's destructor restores the original mode.
  if (cfg.mouse) {
    DWORD mode = 0;
    if (GetConsoleMode(input_mode.input(), &mode)) {
      SetConsoleMode(input_mode.input(),
                     (mode | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS) &
                         ~ENABLE_QUICK_EDIT_MODE);
    }
  }

  size_t top_line = 0;
  int eof_count = 0;
  std::optional<SearchState> last_search;
  std::optional<MatchLocation> last_match;
  std::vector<std::wstring> search_history;

  while (true) {
    auto [current_width, current_height] = console_size();
    width = current_width;
    height = current_height;
    page_size = page_size_for(cfg, height);
    const size_t max_top =
        doc.line_count() > static_cast<size_t>(page_size)
            ? doc.line_count() - static_cast<size_t>(page_size)
            : 0;
    top_line = std::min(top_line, max_top);

    bool at_eof = top_line + static_cast<size_t>(page_size) >= doc.line_count();
    render_page(cfg, doc, top_line, page_size, width,
                last_search ? &*last_search : nullptr,
                last_match ? &*last_match : nullptr);
    print_prompt(cfg, doc, file_index, file_count, top_line, doc.line_count(),
                 static_cast<size_t>(page_size), at_eof);

    if (at_eof && cfg.quit_first_eof) return {0, PagerAction::Quit};

    PagerCommand command =
        read_pager_command(input_mode.input(), cfg.quit_on_intr, search_history,
                           cfg, &doc, top_line);
    PagerAction action = command.action;
    if (action == PagerAction::Quit) return {0, PagerAction::Quit};

    switch (action) {
      case PagerAction::FirstLine:
        top_line = 0;
        last_match.reset();
        eof_count = 0;
        break;
      case PagerAction::LastLine:
        top_line = max_top;
        last_match.reset();
        eof_count = 0;
        break;
      case PagerAction::GoToLine:
        top_line =
            command.number > 0 ? std::min(command.number - 1, max_top) : 0;
        last_match.reset();
        eof_count = 0;
        break;
      case PagerAction::GoToPercent:
        top_line =
            std::min(max_top, doc.line_count() *
                                  std::min<size_t>(command.number, 100) / 100);
        last_match.reset();
        eof_count = 0;
        break;
      case PagerAction::NextFile:
      case PagerAction::PrevFile:
        return {0, action, command.number};
      case PagerAction::NextPage:
        if (at_eof) {
          ++eof_count;
          if (cfg.quit_at_eof && eof_count >= 2) return {0, PagerAction::Quit};
        } else {
          top_line =
              std::min(max_top, top_line + static_cast<size_t>(page_size));
          last_match.reset();
          eof_count = 0;
        }
        break;
      case PagerAction::NextLine:
        if (top_line >= max_top) {
          ++eof_count;
          if (cfg.quit_at_eof && eof_count >= 2) return {0, PagerAction::Quit};
        } else {
          ++top_line;
          last_match.reset();
          eof_count = 0;
        }
        break;
      case PagerAction::NextHalfPage:
        if (top_line >= max_top) {
          ++eof_count;
          if (cfg.quit_at_eof && eof_count >= 2) return {0, PagerAction::Quit};
        } else {
          top_line = std::min(max_top, top_line + static_cast<size_t>(std::max(
                                                      page_size / 2, 1)));
          last_match.reset();
          eof_count = 0;
        }
        break;
      case PagerAction::PrevPage:
        top_line = top_line > static_cast<size_t>(page_size)
                       ? top_line - static_cast<size_t>(page_size)
                       : 0;
        last_match.reset();
        eof_count = 0;
        break;
      case PagerAction::PrevLine:
        if (top_line > 0) --top_line;
        last_match.reset();
        eof_count = 0;
        break;
      case PagerAction::PrevHalfPage: {
        size_t step = static_cast<size_t>(std::max(page_size / 2, 1));
        top_line = top_line > step ? top_line - step : 0;
        last_match.reset();
        eof_count = 0;
        break;
      }
      case PagerAction::WheelDown:
        if (top_line >= max_top) {
          ++eof_count;
          if (cfg.quit_at_eof && eof_count >= 2) {
            return {0, PagerAction::Quit};
          }
        } else {
          top_line = std::min(max_top, top_line + command.number);
          last_match.reset();
          eof_count = 0;
        }
        break;
      case PagerAction::WheelUp:
        top_line = top_line > command.number ? top_line - command.number : 0;
        last_match.reset();
        eof_count = 0;
        break;
      case PagerAction::Follow: {
        // [GNU] F: wait for more data at EOF, like `tail -f`. Only
        // seekable file documents can be followed. The file is polled
        // every 250 ms: growth or truncation reloads it and jumps to
        // the end. If the file is REPLACED (identity change), follow
        // stops unless --follow-name was given, in which case the new
        // file is picked up by name. A keypress ends the wait (the key
        // is not replayed); with -K, a quit key exits the pager.
        if (!doc.seekable) break;
        const std::wstring follow_path =
            utf8_to_wstring(doc.path.empty() ? doc.name : doc.path);
        auto stamp = file_stamp(follow_path);
        if (!stamp) break;
        top_line = max_top;
        bool leave = false;
        while (!leave) {
          render_page(cfg, doc, top_line, page_size, width,
                      last_search ? &*last_search : nullptr,
                      last_match ? &*last_match : nullptr);
          print_prompt(cfg, doc, file_index, file_count, top_line,
                       doc.line_count(), static_cast<size_t>(page_size), true);
          if (WaitForSingleObject(input_mode.input(), 250) == WAIT_OBJECT_0) {
            PagerCommand fc =
                read_pager_command(input_mode.input(), cfg.quit_on_intr,
                                   search_history, cfg, &doc, top_line);
            if (fc.action == PagerAction::Quit) {
              if (cfg.quit_on_intr) return {0, PagerAction::Quit};
              leave = true;
            } else if (fc.action != PagerAction::None) {
              leave = true;
            }
            continue;
          }
          auto now = file_stamp(follow_path);
          if (!now) {
            leave = true;
            break;
          }
          const bool replaced =
              now->volume != stamp->volume || now->index != stamp->index;
          if (replaced && !cfg.follow_name) {
            leave = true;
            break;
          }
          if (replaced || now->size != stamp->size) {
            auto fresh = load_seekable_document(filename);
            if (!fresh) {
              leave = true;
              break;
            }
            doc = std::move(*fresh);
            stamp = now;
            const size_t new_max =
                doc.line_count() > static_cast<size_t>(page_size)
                    ? doc.line_count() - static_cast<size_t>(page_size)
                    : 0;
            top_line = new_max;
          }
        }
        eof_count = 0;
        break;
      }
      case PagerAction::SearchForward:
      case PagerAction::SearchBackward: {
        bool forward = action == PagerAction::SearchForward;
        auto compiled = compile_search(cfg, command.text, forward);
        if (compiled) {
          last_search = std::move(*compiled);
          if (auto match =
                  find_search_match(doc, *last_search, top_line, forward)) {
            last_match = *match;
            top_line = top_line_for_match(match->line, max_top, page_size);
          }
        }
        eof_count = 0;
        break;
      }
      case PagerAction::RepeatSearch:
      case PagerAction::ReverseSearch:
        if (last_search) {
          bool forward = last_search->forward;
          if (action == PagerAction::ReverseSearch) forward = !forward;
          size_t anchor = last_match ? last_match->line : top_line;
          if (auto match =
                  find_search_match(doc, *last_search, anchor, forward)) {
            last_match = *match;
            top_line = top_line_for_match(match->line, max_top, page_size);
          }
        }
        eof_count = 0;
        break;
      case PagerAction::Repaint:
      case PagerAction::Status:
        eof_count = 0;
        break;
      case PagerAction::Quit:
        return {0, PagerAction::Quit};
      case PagerAction::None:
        break;
    }
  }
}

auto run(const Config& cfg) -> int {
  if (cfg.default_stdin && isInputConsole()) {
    safeErrorPrintLn("less: missing filename or piped input");
    safeErrorPrintLn("Usage: less [OPTION]... [FILE]...");
    return 1;
  }

  if (!isOutputConsole()) {
    for (const auto& file : cfg.files) {
      auto content_result = read_file_content(file);
      if (!content_result) {
        cp::report_error(content_result, L"less");
        return 1;
      }
      safePrint(*content_result);
    }
    return 0;
  }

  std::vector<PagerDocument> docs;
  docs.reserve(cfg.files.size());
  for (const auto& file : cfg.files) {
    auto doc = load_interactive_document(file);
    if (!doc) {
      safeErrorPrint("less: ");
      safeErrorPrintLn(doc.error());
      return 1;
    }
    docs.push_back(std::move(*doc));
  }

  size_t index = 0;
  while (index < docs.size()) {
    PagerResult result =
        simple_pager(cfg, docs[index], cfg.files[index], index, docs.size());
    if (result.code != 0) return result.code;
    if (result.action == PagerAction::NextFile) {
      size_t count = result.count == 0 ? 1 : result.count;
      if (index + count < docs.size()) index += count;
      continue;
    }
    if (result.action == PagerAction::PrevFile) {
      size_t count = result.count == 0 ? 1 : result.count;
      index = index > count ? index - count : 0;
      continue;
    }
    break;
  }

  return 0;
}

}  // namespace less_pipeline

REGISTER_COMMAND(
    less, "less", "less [OPTION]... [FILE]...",
    "A pager for viewing files, similar to more but with more features.\n"
    "Allows backward movement in the file as well as forward movement.\n"
    "\n"
    "Interactive commands include SPACE/f, ENTER/j/e, k/y, b, d/u, g/G, "
    "NUMg, NUM%, /, ?, n, N, :n, :p, :f, =, Ctrl-G, r/R, F, and q.\n"
    "F waits for more data at end-of-file (follow mode); with\n"
    "--follow-name a replaced file is picked up by name.  --file-size\n"
    "adds byte counts to the prompt, --line-num-width sets the width\n"
    "of the -N number field, --rscroll picks the character shown at\n"
    "the edge of a chopped (-S) line, --mouse enables wheel scrolling,\n"
    "--incsearch reports the match line while a pattern is typed, and\n"
    "--no-histdups keeps the search history free of duplicates.",
    "  less file.txt\n"
    "  less -N file.txt          # Show line numbers\n"
    "  less -E file.txt          # Quit at end of file\n"
    "  less -F file.txt          # Quit if fits on one screen\n"
    "  less -K file.txt          # Quit on Ctrl+C",
    "more(1), most(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", LESS_OPTIONS) {
  using namespace less_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"less");
    return 1;
  }

  return run(*cfg_result);
}
