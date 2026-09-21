// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
module;
#include "pch/pch.h"

export module utils:pager;

import std;
import :console;
import :i18n;

namespace winux::pager {
export struct Options {
  std::string title;
  size_t fixed_page_size = 0;
  bool quit_if_one_screen = false;
};

export class ConsoleInputMode {
 public:
  explicit ConsoleInputMode(bool processed_input = true)
      : input_(GetStdHandle(STD_INPUT_HANDLE)) {
    if (input_ == INVALID_HANDLE_VALUE || input_ == nullptr) {
      input_ = nullptr;
    }

    if (!try_activate(processed_input)) {
      // Match less(1): piped input is content; pager commands still read from
      // the controlling console.
      input_ = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
      if (input_ == INVALID_HANDLE_VALUE) {
        input_ = nullptr;
        return;
      }
      close_input_ = true;
      (void)try_activate(processed_input);
    }
  }

  ~ConsoleInputMode() {
    if (active_) SetConsoleMode(input_, original_mode_);
    if (close_input_ && input_ != nullptr) CloseHandle(input_);
  }

  [[nodiscard]] auto active() const -> bool { return active_; }
  [[nodiscard]] auto input() const -> HANDLE { return input_; }

 private:
  auto try_activate(bool processed_input) -> bool {
    if (input_ == nullptr) return false;
    if (!GetConsoleMode(input_, &original_mode_)) return false;
    DWORD raw_mode = original_mode_ & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    if (!processed_input) raw_mode &= ~ENABLE_PROCESSED_INPUT;
    active_ = SetConsoleMode(input_, raw_mode) != 0;
    return active_;
  }

  HANDLE input_ = nullptr;
  DWORD original_mode_ = 0;
  bool active_ = false;
  bool close_input_ = false;
};

export auto split_text_lines(std::string_view content)
    -> std::vector<std::string> {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < content.size()) {
    size_t end = content.find('\n', start);
    if (end == std::string_view::npos) {
      lines.emplace_back(content.substr(start));
      break;
    }

    std::string_view line = content.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    lines.emplace_back(line);
    start = end + 1;
  }

  if (content.ends_with('\n')) lines.emplace_back();
  if (lines.empty()) lines.emplace_back();
  return lines;
}

export class Document {
 public:
  std::string name;
  bool seekable = false;
  std::string content;
  std::vector<std::string> memory_lines;
  std::vector<std::uint64_t> line_offsets;
  std::uint64_t file_size = 0;

  [[nodiscard]] auto line_count() const -> size_t {
    return seekable ? line_offsets.size() : memory_lines.size();
  }

  [[nodiscard]] auto line_at(size_t index) const
      -> std::expected<std::string, std::string> {
    if (index >= line_count()) return std::string{};
    if (!seekable) return memory_lines[index];

    std::ifstream file(name, std::ios::binary);
    if (!file) {
      return std::unexpected(std::string("cannot open '") + name +
                             "' for reading");
    }

    std::uint64_t start = line_offsets[index];
    std::uint64_t end = index + 1 < line_offsets.size()
                            ? line_offsets[index + 1] - 1
                            : file_size;
    if (end < start) end = start;

    std::string line(static_cast<size_t>(end - start), '\0');
    file.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    if (!line.empty()) {
      file.read(line.data(), static_cast<std::streamsize>(line.size()));
      if (file.bad()) return std::unexpected("error reading from file");
    }
    if (!line.empty() && line.back() == '\n') line.pop_back();
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
  }

  [[nodiscard]] auto materialize_lines() const
      -> std::expected<std::vector<std::string>, std::string> {
    std::vector<std::string> lines;
    lines.reserve(line_count());
    for (size_t i = 0; i < line_count(); ++i) {
      auto line = line_at(i);
      if (!line) return std::unexpected(line.error());
      lines.push_back(std::move(*line));
    }
    return lines;
  }
};

export auto load_memory_document(std::string name, std::string content)
    -> Document {
  Document doc;
  doc.name = std::move(name);
  doc.content = std::move(content);
  doc.memory_lines = split_text_lines(doc.content);
  return doc;
}

export auto load_seekable_document(const std::string& filename)
    -> std::expected<Document, std::string> {
  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    return std::unexpected(std::string("cannot open '") + filename +
                           "' for reading");
  }

  file.seekg(0, std::ios::end);
  std::streamoff end_pos = file.tellg();
  if (end_pos < 0) return std::unexpected("error reading from file");

  Document doc;
  doc.name = filename;
  doc.seekable = true;
  doc.file_size = static_cast<std::uint64_t>(end_pos);
  doc.line_offsets.push_back(0);

  file.seekg(0, std::ios::beg);
  std::array<char, 64 * 1024> buffer{};
  std::uint64_t absolute_pos = 0;
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::streamsize count = file.gcount();
    for (std::streamsize i = 0; i < count; ++i) {
      std::uint64_t next_pos = absolute_pos + static_cast<std::uint64_t>(i) + 1;
      if (buffer[static_cast<size_t>(i)] == '\n' && next_pos < doc.file_size) {
        doc.line_offsets.push_back(next_pos);
      }
    }
    absolute_pos += static_cast<std::uint64_t>(count);
  }

  if (file.bad()) return std::unexpected("error reading from file");
  return doc;
}

namespace {
auto clamp_to_width(std::string_view line, int width) -> std::string_view {
  if (width <= 0 || line.size() <= static_cast<size_t>(width)) return line;
  return line.substr(0, static_cast<size_t>(width));
}

enum class Action {
  None,
  NextPage,
  NextLine,
  NextHalfPage,
  PrevPage,
  PrevLine,
  PrevHalfPage,
  FirstLine,
  LastLine,
  SearchForward,
  SearchBackward,
  RepeatSearch,
  ReverseSearch,
  Repaint,
  Status,
  Quit
};

struct Command {
  Action action = Action::None;
  std::string text;
};

struct SearchState {
  std::string text;
  bool forward = true;
};

struct MatchLocation {
  size_t line = 0;
  size_t begin = 0;
  size_t end = 0;
};

auto redraw_prompt_text(wchar_t prompt, std::wstring_view text) -> void {
  safePrint("\r\033[K");
  safePrint(std::wstring_view(&prompt, 1));
  safePrint(text);
}

auto push_history(std::vector<std::wstring>& history, std::wstring_view text)
    -> void {
  if (text.empty()) return;
  if (!history.empty() && history.back() == text) return;
  history.emplace_back(text);
}

auto read_prompt_text(HANDLE input, wchar_t prompt,
                      std::vector<std::wstring>& history)
    -> std::optional<std::string> {
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
    if (key.wVirtualKeyCode == VK_ESCAPE) return std::nullopt;
    if (key.wVirtualKeyCode == VK_RETURN) {
      safePrint("\n");
      push_history(history, text);
      return wstring_to_utf8(text);
    }
    if (key.wVirtualKeyCode == VK_UP || key.wVirtualKeyCode == VK_DOWN) {
      if (history.empty()) continue;
      if (!draft && history_index == history.size()) draft = text;

      if (key.wVirtualKeyCode == VK_UP) {
        if (history_index > 0) --history_index;
      } else if (history_index < history.size()) {
        ++history_index;
      }

      text = history_index < history.size() ? history[history_index]
                                            : draft.value_or(std::wstring{});
      redraw_prompt_text(prompt, text);
      continue;
    }
    if (key.wVirtualKeyCode == VK_BACK) {
      if (!text.empty()) {
        text.pop_back();
        history_index = history.size();
        draft.reset();
        redraw_prompt_text(prompt, text);
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
  return std::nullopt;
}

auto read_action(HANDLE input, std::vector<std::wstring>& search_history)
    -> Command {
  INPUT_RECORD record{};
  DWORD read = 0;
  while (ReadConsoleInputW(input, &record, 1, &read)) {
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
      continue;
    }

    const auto& key = record.Event.KeyEvent;
    char ch = key.uChar.AsciiChar;
    if (ch == 'q' || ch == 'Q') return {Action::Quit};
    if (ch == ' ' || ch == 'f' || ch == 6 || ch == 22) {
      return {Action::NextPage};
    }
    if (ch == '\r' || ch == '\n' || ch == 'j' || ch == 'e' || ch == 5 ||
        ch == 14) {
      return {Action::NextLine};
    }
    if (ch == 'k' || ch == 'y' || ch == 25 || ch == 16) {
      return {Action::PrevLine};
    }
    if (ch == 'b' || ch == 'B' || ch == 2) return {Action::PrevPage};
    if (ch == 'd' || ch == 4) return {Action::NextHalfPage};
    if (ch == 'u' || ch == 21) return {Action::PrevHalfPage};
    if (ch == 'g' || ch == '<') return {Action::FirstLine};
    if (ch == 'G' || ch == '>') return {Action::LastLine};
    if (ch == 'r' || ch == 'R' || ch == 12 || ch == 18) {
      return {Action::Repaint};
    }
    if (ch == '=' || ch == 7) return {Action::Status};
    if (ch == 'n') return {Action::RepeatSearch};
    if (ch == 'N') return {Action::ReverseSearch};
    if (ch == '/') {
      auto text = read_prompt_text(input, L'/', search_history);
      return text ? Command{Action::SearchForward, *text} : Command{};
    }
    if (ch == '?') {
      auto text = read_prompt_text(input, L'?', search_history);
      return text ? Command{Action::SearchBackward, *text} : Command{};
    }

    switch (key.wVirtualKeyCode) {
      case VK_NEXT:
        return {Action::NextPage};
      case VK_PRIOR:
        return {Action::PrevPage};
      case VK_DOWN:
        return {Action::NextLine};
      case VK_UP:
        return {Action::PrevLine};
      case VK_HOME:
        return {Action::FirstLine};
      case VK_END:
        return {Action::LastLine};
      case VK_ESCAPE:
        return {Action::Quit};
      default:
        return {};
    }
  }

  return {Action::Quit};
}

auto page_size_for(const Options& options, int height) -> size_t {
  size_t visible = static_cast<size_t>(std::max(height - 1, 1));
  if (options.fixed_page_size == 0) return visible;
  return std::clamp(options.fixed_page_size, size_t{1}, visible);
}

auto max_top_for(size_t line_count, size_t page_size) -> size_t {
  return line_count > page_size ? line_count - page_size : 0;
}

auto top_line_for_match(size_t match_line, size_t max_top, size_t page_size)
    -> size_t {
  size_t context = page_size > 4 ? page_size / 3 : 0;
  size_t target = match_line > context ? match_line - context : 0;
  return std::min(target, max_top);
}

auto find_match(const std::vector<std::string>& lines,
                const SearchState& search, size_t anchor_line, bool forward)
    -> std::optional<MatchLocation> {
  if (lines.empty() || search.text.empty()) return std::nullopt;

  auto line_match = [&](size_t index) -> std::optional<MatchLocation> {
    size_t pos = lines[index].find(search.text);
    if (pos == std::string_view::npos) return std::nullopt;
    return MatchLocation{index, pos, pos + search.text.size()};
  };

  if (forward) {
    size_t start = std::min(anchor_line + 1, lines.size());
    for (size_t i = start; i < lines.size(); ++i) {
      if (auto match = line_match(i)) return match;
    }
    for (size_t i = 0; i < start; ++i) {
      if (auto match = line_match(i)) return match;
    }
  } else {
    size_t start = anchor_line == 0 ? lines.size() - 1 : anchor_line - 1;
    for (size_t i = start + 1; i-- > 0;) {
      if (auto match = line_match(i)) return match;
      if (i == 0) break;
    }
    for (size_t i = lines.size(); i-- > start + 1;) {
      if (auto match = line_match(i)) return match;
    }
  }

  return std::nullopt;
}

auto highlighted_line(std::string_view line, const SearchState* search,
                      std::optional<std::pair<size_t, size_t>> current_range)
    -> std::string {
  if (!search || search->text.empty() || !shouldUseAnsiColorStdout()) {
    return std::string(line);
  }

  std::string out;
  size_t cursor = 0;
  while (cursor < line.size()) {
    size_t pos = line.find(search->text, cursor);
    if (pos == std::string_view::npos) {
      out.append(line.substr(cursor));
      break;
    }

    out.append(line.substr(cursor, pos - cursor));
    const bool is_current = current_range && current_range->first == pos &&
                            current_range->second == pos + search->text.size();
    out += is_current ? "\033[1;30;43m" : "\033[7m";
    out.append(line.substr(pos, search->text.size()));
    out += ANSI_RESET;
    cursor = pos + search->text.size();
  }
  return out;
}

auto render(const Options& options, const std::vector<std::string>& lines,
            size_t top_line, size_t page_size, int width,
            const SearchState* search, const MatchLocation* current_match)
    -> void {
  clearConsoleViewport();

  for (size_t i = 0; i < page_size; ++i) {
    size_t line_index = top_line + i;
    if (line_index >= lines.size()) {
      safePrintLn("");
      continue;
    }

    auto line = clamp_to_width(lines[line_index], width);
    std::optional<std::pair<size_t, size_t>> current_range;
    if (current_match && current_match->line == line_index &&
        current_match->begin < line.size()) {
      current_range = std::pair{
          current_match->begin,
          std::min(current_match->end, static_cast<size_t>(line.size()))};
    }
    safePrintLn(highlighted_line(line, search, current_range));
  }

  bool at_eof = top_line + page_size >= lines.size();
  safePrint(":");
  if (at_eof) safePrint(winux::i18n::translate("utils.pager.end", "(END) "));
  if (!options.title.empty()) {
    safePrint(options.title);
    safePrint("  ");
  }
  size_t top_display = lines.empty() ? 0 : top_line + 1;
  size_t bottom_line =
      lines.empty() ? 0 : std::min(lines.size(), top_line + page_size);
  safePrint(winux::i18n::translate("utils.pager.lines", "lines "));
  safePrint(std::to_string(top_display));
  safePrint("-");
  safePrint(std::to_string(bottom_line));
  safePrint("/");
  safePrint(std::to_string(lines.size()));
  safePrint(winux::i18n::translate(
      "utils.pager.controls",
      "  SPACE/f:next  j/k:line  b:back  /:search  n/N  g/G  q"));
}
}  // namespace

export auto page_text(std::string_view content, const Options& options = {})
    -> int {
  if (!isOutputConsole() || !isInputConsole()) {
    safePrint(content);
    return 0;
  }

  auto lines = split_text_lines(content);
  auto [initial_width, initial_height] = getConsoleViewportSize();
  size_t page_size = page_size_for(options, initial_height);
  if (options.quit_if_one_screen && lines.size() <= page_size) {
    safePrint(content);
    return 0;
  }

  ConsoleInputMode input_mode;
  if (!input_mode.active()) {
    safePrint(content);
    return 0;
  }

  size_t top_line = 0;
  std::optional<SearchState> last_search;
  std::optional<MatchLocation> last_match;
  std::vector<std::wstring> search_history;
  while (true) {
    auto [width, height] = getConsoleViewportSize();
    page_size = page_size_for(options, height);
    const size_t max_top = max_top_for(lines.size(), page_size);
    top_line = std::min(top_line, max_top);
    render(options, lines, top_line, page_size, width,
           last_search ? &*last_search : nullptr,
           last_match ? &*last_match : nullptr);

    Command command = read_action(input_mode.input(), search_history);
    switch (command.action) {
      case Action::NextPage:
        top_line = std::min(max_top, top_line + page_size);
        last_match.reset();
        break;
      case Action::NextLine:
        top_line = std::min(max_top, top_line + 1);
        last_match.reset();
        break;
      case Action::NextHalfPage:
        top_line =
            std::min(max_top, top_line + std::max<size_t>(1, page_size / 2));
        last_match.reset();
        break;
      case Action::PrevPage:
        top_line = top_line > page_size ? top_line - page_size : 0;
        last_match.reset();
        break;
      case Action::PrevLine:
        if (top_line > 0) --top_line;
        last_match.reset();
        break;
      case Action::PrevHalfPage: {
        size_t step = std::max<size_t>(1, page_size / 2);
        top_line = top_line > step ? top_line - step : 0;
        last_match.reset();
        break;
      }
      case Action::FirstLine:
        top_line = 0;
        last_match.reset();
        break;
      case Action::LastLine:
        top_line = max_top;
        last_match.reset();
        break;
      case Action::SearchForward:
      case Action::SearchBackward: {
        if (command.text.empty()) break;
        bool forward = command.action == Action::SearchForward;
        last_search = SearchState{std::move(command.text), forward};
        if (auto match = find_match(lines, *last_search, top_line, forward)) {
          last_match = *match;
          top_line = top_line_for_match(match->line, max_top, page_size);
        }
        break;
      }
      case Action::RepeatSearch:
      case Action::ReverseSearch:
        if (last_search) {
          bool forward = last_search->forward;
          if (command.action == Action::ReverseSearch) forward = !forward;
          size_t anchor = last_match ? last_match->line : top_line;
          if (auto match = find_match(lines, *last_search, anchor, forward)) {
            last_match = *match;
            top_line = top_line_for_match(match->line, max_top, page_size);
          }
        }
        break;
      case Action::Repaint:
      case Action::Status:
        break;
      case Action::Quit:
        clearConsoleViewport();
        return 0;
      case Action::None:
        break;
    }
  }
}
}  // namespace winux::pager
