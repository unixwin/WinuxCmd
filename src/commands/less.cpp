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
    OPTION("-NUM", "", "same as -z NUM", INT_TYPE)};

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
    // Read from stdin
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      return std::unexpected("error reading from standard input");
    }
  } else {
    // Read from file
    std::ifstream file(filename, std::ios::binary);
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

auto redraw_search_text(wchar_t prompt, std::wstring_view text) -> void {
  safePrint("\r\033[K");
  safePrint(std::wstring_view(&prompt, 1));
  safePrint(text);
}

auto push_search_history(std::vector<std::wstring>& history,
                         std::wstring_view text) -> void {
  if (text.empty()) return;
  if (!history.empty() && history.back() == text) return;
  history.emplace_back(text);
}

[[nodiscard]] auto is_interrupt_key(const KEY_EVENT_RECORD& key) -> bool {
  return key.uChar.AsciiChar == 3 || key.wVirtualKeyCode == VK_CANCEL;
}

struct PromptResult {
  PagerAction action = PagerAction::None;
  std::string text;
};

auto read_search_text(HANDLE input, wchar_t prompt, bool quit_on_intr,
                      std::vector<std::wstring>& history) -> PromptResult {
  std::wstring text;
  std::optional<std::wstring> draft;
  size_t history_index = history.size();
  safePrint("\r\033[K");
  safePrint(std::wstring_view(&prompt, 1));

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
      push_search_history(history, text);
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
      redraw_search_text(prompt, text);
      continue;
    }
    if (key.wVirtualKeyCode == VK_BACK) {
      if (!text.empty()) {
        text.pop_back();
        history_index = history.size();
        draft.reset();
        redraw_search_text(prompt, text);
      }
      continue;
    }

    wchar_t ch = key.uChar.UnicodeChar;
    if (ch >= L' ') {
      text.push_back(ch);
      history_index = history.size();
      draft.reset();
      safePrint(std::wstring_view(&ch, 1));
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
                        std::vector<std::wstring>& search_history)
    -> PagerCommand {
  INPUT_RECORD record{};
  DWORD read = 0;
  while (ReadConsoleInputW(input, &record, 1, &read)) {
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
      continue;
    }

    const auto& key = record.Event.KeyEvent;
    if (quit_on_intr && is_interrupt_key(key)) return {PagerAction::Quit, {}};
    char ch = key.uChar.AsciiChar;
    if (ch == 'q' || ch == 'Q') return {PagerAction::Quit, {}};
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
    if (ch == '/') {
      auto result = read_search_text(input, L'/', quit_on_intr, search_history);
      return result.action == PagerAction::Quit
                 ? PagerCommand{PagerAction::Quit, {}}
                 : PagerCommand{PagerAction::SearchForward, result.text};
    }
    if (ch == '?') {
      auto result = read_search_text(input, L'?', quit_on_intr, search_history);
      return result.action == PagerAction::Quit
                 ? PagerCommand{PagerAction::Quit, {}}
                 : PagerCommand{PagerAction::SearchBackward, result.text};
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
    auto prefix = std::format("{:>6}\t", line_index + 1);
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

auto print_prompt(const PagerDocument& doc, size_t file_index,
                  size_t file_count, size_t top_line, size_t total_lines,
                  size_t page_size, bool at_eof) -> void {
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

auto simple_pager(const Config& cfg, const PagerDocument& doc,
                  size_t file_index, size_t file_count) -> PagerResult {
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
    print_prompt(doc, file_index, file_count, top_line, doc.line_count(),
                 static_cast<size_t>(page_size), at_eof);

    if (at_eof && cfg.quit_first_eof) return {0, PagerAction::Quit};

    PagerCommand command = read_pager_command(input_mode.input(),
                                              cfg.quit_on_intr, search_history);
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
    PagerResult result = simple_pager(cfg, docs[index], index, docs.size());
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
    "NUMg, NUM%, /, ?, n, N, :n, :p, :f, =, Ctrl-G, r/R, and q.",
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
