// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for more.
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

auto constexpr MORE_OPTIONS = std::array{
    // [EXT]
    OPTION("-d", "--silent", "display help instead of ring bell", BOOL_TYPE),
    // [EXT]
    OPTION("-f", "--logical", "count logical lines, not screen lines",
           BOOL_TYPE),
    // [EXT]
    OPTION("-l", "--no-pause", "suppress pause after form feeds", BOOL_TYPE),
    // [EXT]
    OPTION("-c", "--print-over", "clear line ends before each page", BOOL_TYPE),
    // [EXT]
    OPTION("-p", "--clean-print",
           "do not scroll, clean screen and display text", BOOL_TYPE),
    // [EXT]
    OPTION("-e", "--exit-on-eof", "exit on end-of-file", BOOL_TYPE),
    // [EXT]
    OPTION("-s", "--squeeze", "squeeze multiple blank lines", BOOL_TYPE),
    // [EXT]
    OPTION("-u", "--plain",
           "accepted placeholder; underlining and bold are already plain",
           BOOL_TYPE),
    // [EXT]
    OPTION("-n", "--lines", "number of lines per screenful", INT_TYPE),
    // [EXT]
    OPTION("-NUM", "", "same as -n NUM", INT_TYPE)};

namespace more_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool help_prompt = false;
  bool logical_lines = false;
  bool pause_form_feed = false;
  bool clear_screen = false;
  bool no_scroll = false;
  bool exit_on_eof = false;
  bool squeeze_blank = false;
  bool plain = false;
  size_t lines_per_page = 24;
  size_t start_line = 0;
  std::string search_pattern;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<MORE_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.help_prompt =
      ctx.get<bool>("--silent", false) || ctx.get<bool>("-d", false);
  cfg.logical_lines =
      ctx.get<bool>("--logical", false) || ctx.get<bool>("-f", false);
  cfg.pause_form_feed =
      ctx.get<bool>("--no-pause", false) || ctx.get<bool>("-l", false);
  cfg.clear_screen =
      ctx.get<bool>("--print-over", false) || ctx.get<bool>("-c", false);
  cfg.no_scroll =
      ctx.get<bool>("--clean-print", false) || ctx.get<bool>("-p", false);
  cfg.exit_on_eof =
      ctx.get<bool>("--exit-on-eof", false) || ctx.get<bool>("-e", false);
  cfg.squeeze_blank =
      ctx.get<bool>("--squeeze", false) || ctx.get<bool>("-s", false);
  cfg.plain = ctx.get<bool>("--plain", false) || ctx.get<bool>("-u", false);

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= MORE_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    const auto* value = std::get_if<int>(&occurrence.value);
    if (!value) continue;
    if (option_matches(meta, "-n", "--lines") ||
        option_matches(meta, "-NUM", "")) {
      if (*value < 1) return std::unexpected("invalid line count");
      cfg.lines_per_page = static_cast<size_t>(*value);
    }
  }

  for (const auto& pos : ctx.positionals) {
    std::string arg(pos);
    if (arg.size() > 1 && arg[0] == '+') {
      if (arg[1] == '/') {
        cfg.search_pattern = arg.substr(2);
        continue;
      }
      bool digits_only = std::ranges::all_of(
          arg.begin() + 1, arg.end(),
          [](unsigned char ch) { return std::isdigit(ch) != 0; });
      if (digits_only) {
        auto value = std::string_view(arg).substr(1);
        unsigned long long line = 0;
        auto [ptr, ec] =
            std::from_chars(value.data(), value.data() + value.size(), line);
        if (ec != std::errc() || ptr != value.data() + value.size()) {
          return std::unexpected("invalid line number");
        }
        cfg.start_line = line > 0 ? static_cast<size_t>(line - 1) : 0;
        continue;
      }
    }
    cfg.files.push_back(std::move(arg));
  }

  return cfg;
}

auto get_console_height() -> size_t {
  auto [width, height] = getConsoleViewportSize();
  (void)width;
  return static_cast<size_t>(std::max(height - 1, 1));
}

auto estimate_screen_rows(const std::string& line, size_t console_width)
    -> size_t {
  if (line.empty()) return 1;
  return (line.length() + console_width - 1) / console_width;
}

auto clear_console() -> void { clearConsoleViewport(); }

enum class MoreAction {
  None,
  RepeatPrevious,
  NextPage,
  SetPageSize,
  NextLine,
  Scroll,
  SkipScreens,
  SkipLines,
  PrevPage,
  Search,
  RepeatSearch,
  DisplayLine,
  DisplayFile,
  Help,
  ClearScreen,
  NextFile,
  PrevFile,
  Quit
};

struct MoreCommand {
  MoreAction action = MoreAction::None;
  size_t number = 0;
  std::string text;
};

struct MoreResult {
  int code = 0;
  MoreAction action = MoreAction::Quit;
  size_t count = 0;
};

auto print_runtime_help() -> void {
  safePrintLn(
      "Most commands optionally preceded by integer argument k. Defaults in "
      "brackets.");
  safePrintLn("SPACE       display next k lines [current screen size]");
  safePrintLn("z           display next k lines and set screen size");
  safePrintLn("ENTER       display next k lines [1]");
  safePrintLn("d, Ctrl-D   scroll k lines [half screen]");
  safePrintLn("s           skip forward k lines [1]");
  safePrintLn("f, Ctrl-F   skip forward k screenfuls [1]");
  safePrintLn("b, Ctrl-B   skip backward k screenfuls [1]");
  safePrintLn("/pattern    search for kth occurrence [1]");
  safePrintLn("n           repeat previous search");
  safePrintLn("=           display current line number");
  safePrintLn(":f          display current file and line");
  safePrintLn(":n, :p      next/previous file");
  safePrintLn(".           repeat previous command");
  safePrintLn("q           quit");
}

auto read_prompt_text(HANDLE input, char prompt) -> std::optional<std::string> {
  std::wstring text;
  safePrint(std::string_view(&prompt, 1));

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
      return wstring_to_utf8(text);
    }
    if (key.wVirtualKeyCode == VK_BACK) {
      if (!text.empty()) {
        text.pop_back();
        safePrint("\r\033[K");
        safePrint(std::string_view(&prompt, 1));
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
  return std::nullopt;
}

auto read_more_command(HANDLE input) -> MoreCommand {
  std::string digits;

  INPUT_RECORD record{};
  DWORD read = 0;
  while (ReadConsoleInputW(input, &record, 1, &read)) {
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
      continue;
    }

    const auto& key = record.Event.KeyEvent;
    char ch = key.uChar.AsciiChar;
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      digits.push_back(ch);
      continue;
    }

    auto number = [&]() -> size_t {
      if (digits.empty()) return 0;
      size_t value = 0;
      auto [ptr, ec] =
          std::from_chars(digits.data(), digits.data() + digits.size(), value);
      return ec == std::errc() && ptr == digits.data() + digits.size() ? value
                                                                       : 0;
    }();

    if (ch == ':') {
      INPUT_RECORD colon_record{};
      DWORD colon_read = 0;
      while (ReadConsoleInputW(input, &colon_record, 1, &colon_read)) {
        if (colon_record.EventType != KEY_EVENT ||
            !colon_record.Event.KeyEvent.bKeyDown) {
          continue;
        }
        char colon_ch = colon_record.Event.KeyEvent.uChar.AsciiChar;
        if (colon_ch == 'n') return {MoreAction::NextFile, number};
        if (colon_ch == 'p') return {MoreAction::PrevFile, number};
        if (colon_ch == 'f') return {MoreAction::DisplayFile, number};
        return {MoreAction::None, number};
      }
      return {MoreAction::Quit, number};
    }

    if (ch == '.') return {MoreAction::RepeatPrevious, number};
    if (ch == 'q' || ch == 'Q') return {MoreAction::Quit, number};
    if (ch == ' ') return {MoreAction::NextPage, number};
    if (ch == 'z') return {MoreAction::SetPageSize, number};
    if (ch == '\r' || ch == '\n') return {MoreAction::NextLine, number};
    if (ch == 'd' || ch == 4) return {MoreAction::Scroll, number};
    if (ch == 's') return {MoreAction::SkipLines, number};
    if (ch == 'f' || ch == 6) return {MoreAction::SkipScreens, number};
    if (ch == 'b' || ch == 2) return {MoreAction::PrevPage, number};
    if (ch == '/' || ch == 'n') {
      if (ch == 'n') return {MoreAction::RepeatSearch, number};
      auto text = read_prompt_text(input, '/');
      return text ? MoreCommand{MoreAction::Search, number, *text}
                  : MoreCommand{MoreAction::None, number};
    }
    if (ch == '=') return {MoreAction::DisplayLine, number};
    if (ch == 'h' || ch == '?') return {MoreAction::Help, number};
    if (ch == '\f' || ch == 12) return {MoreAction::ClearScreen, number};

    switch (key.wVirtualKeyCode) {
      case VK_NEXT:
      case VK_DOWN:
        return {MoreAction::NextPage, number};
      case VK_PRIOR:
      case VK_UP:
        return {MoreAction::PrevPage, number};
      case VK_ESCAPE:
        return {MoreAction::Quit, number};
      default:
        return {MoreAction::None, number};
    }
  }
  return {MoreAction::Quit, 0};
}

auto copy_stream_to_stdout(std::istream& input) -> int {
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto count = input.gcount();
    if (count > 0) {
      safePrint(std::string_view(buffer.data(), static_cast<size_t>(count)));
    }
  }

  return input.bad() ? 1 : 0;
}

// [util-linux more] classify a file operand before opening: directories are
// announced with a "*** name: directory ***" banner and skipped, a trailing
// separator on a regular file is ENOTDIR, and other unreadable operands
// report "cannot open <name>: <err>" (uutils #12786).
struct MoreFileProbe {
  enum class Kind { Openable, Directory, OpenError } kind;
  std::string error;
};

auto probe_file_operand(const std::string& filename) -> MoreFileProbe {
  auto operand = native_path::make_api_path_operand(filename);
  const DWORD attrs = native_path::operand_target_attributes_w(operand);
  if (native_path::attributes_are_directory(attrs)) {
    return {MoreFileProbe::Kind::Directory, {}};
  }
  if (const int err = native_path::operand_file_open_error(operand); err != 0) {
    return {MoreFileProbe::Kind::OpenError,
            err == ENOTDIR ? "Not a directory" : "No such file or directory"};
  }
  if (!native_path::valid_attributes(attrs)) {
    return {MoreFileProbe::Kind::OpenError, "No such file or directory"};
  }
  return {MoreFileProbe::Kind::Openable, {}};
}

auto report_cannot_open(const std::string& filename, std::string_view error)
    -> int {
  safeErrorPrintLn("more: cannot open " + filename + ": " + std::string(error));
  return 1;
}

void print_directory_banner(const std::string& filename) {
  safePrint("\n*** " + filename + ": directory ***\n\n");
}

auto copy_file_to_stdout(const std::string& filename) -> int {
  if (filename.empty() || filename == "-") {
    return copy_stream_to_stdout(std::cin);
  }
  const auto probe = probe_file_operand(filename);
  if (probe.kind == MoreFileProbe::Kind::Directory) {
    print_directory_banner(filename);
    return 0;
  }
  if (probe.kind == MoreFileProbe::Kind::OpenError) {
    return report_cannot_open(filename, probe.error);
  }
  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    return report_cannot_open(filename, "Permission denied");
  }
  return copy_stream_to_stdout(file);
}
auto read_display_lines(const std::string& filename)
    -> std::expected<std::vector<std::string>, std::string> {
  if (filename.empty() || filename == "-") {
    std::string content;
    std::array<char, 64 * 1024> buffer{};
    while (std::cin.good()) {
      std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      auto count = std::cin.gcount();
      if (count > 0) {
        content.append(buffer.data(), static_cast<size_t>(count));
      }
    }
    if (std::cin.bad()) return std::unexpected("error reading from stdin");
    return winux::pager::split_text_lines(content);
  }

  auto doc = winux::pager::load_seekable_document(filename);
  if (!doc) return std::unexpected(doc.error());
  return doc->materialize_lines();
}
auto prepare_start_line(std::vector<std::string>& lines, const Config& cfg)
    -> size_t {
  if (cfg.squeeze_blank) {
    std::vector<std::string> squeezed;
    bool prev_blank = false;
    for (const auto& line : lines) {
      bool is_blank = line.empty();
      if (!is_blank || !prev_blank) {
        squeezed.push_back(line);
      }
      prev_blank = is_blank;
    }
    lines = std::move(squeezed);
  }

  size_t start_line = std::min(cfg.start_line, lines.size());
  if (!cfg.search_pattern.empty()) {
    for (size_t i = start_line; i < lines.size(); ++i) {
      if (lines[i].find(cfg.search_pattern) != std::string::npos) {
        return i;
      }
    }
  }
  return start_line;
}
auto display_file_noninteractive(const std::string& filename, const Config& cfg)
    -> int {
  if (!filename.empty() && filename != "-") {
    const auto probe = probe_file_operand(filename);
    if (probe.kind == MoreFileProbe::Kind::Directory) {
      print_directory_banner(filename);
      return 0;
    }
    if (probe.kind == MoreFileProbe::Kind::OpenError) {
      return report_cannot_open(filename, probe.error);
    }
  }
  auto lines_result = read_display_lines(filename);
  if (!lines_result) {
    safeErrorPrint("more: ");
    safeErrorPrintLn(lines_result.error());
    return 1;
  }
  auto lines = std::move(*lines_result);
  size_t start_line = prepare_start_line(lines, cfg);
  for (size_t i = start_line; i < lines.size(); ++i) {
    safePrintLn(lines[i]);
  }
  return 0;
}
auto display_file(const std::string& filename, const Config& cfg,
                  size_t file_index, size_t file_count) -> MoreResult {
  if (!filename.empty() && filename != "-") {
    const auto probe = probe_file_operand(filename);
    if (probe.kind == MoreFileProbe::Kind::Directory) {
      print_directory_banner(filename);
      return {0, MoreAction::NextFile, 1};
    }
    if (probe.kind == MoreFileProbe::Kind::OpenError) {
      report_cannot_open(filename, probe.error);
      return {1, MoreAction::NextFile, 1};
    }
  }
  auto lines_result = read_display_lines(filename);
  if (!lines_result) {
    safeErrorPrint("more: ");
    safeErrorPrintLn(lines_result.error());
    return {1, MoreAction::Quit};
  }
  auto lines = std::move(*lines_result);
  size_t start_line = prepare_start_line(lines, cfg);

  size_t current_line = start_line;
  size_t lines_per_page = cfg.lines_per_page;
  size_t last_lines_shown = 0;
  size_t scroll_len = std::max<size_t>(1, get_console_height() / 2);
  std::string last_search = cfg.search_pattern;
  MoreCommand previous_command{MoreAction::NextPage, 0};

  winux::pager::ConsoleInputMode input_mode;
  if (!input_mode.active()) {
    for (size_t i = current_line; i < lines.size(); ++i) {
      safePrintLn(lines[i]);
    }
    return {0, MoreAction::Quit};
  }

  while (current_line < lines.size()) {
    size_t visible_height = get_console_height();
    size_t page_height = lines_per_page > 0
                             ? std::min(lines_per_page, visible_height)
                             : visible_height;

    if (cfg.clear_screen || cfg.no_scroll) {
      clear_console();
    }

    size_t lines_shown = 0;
    auto [console_width, console_height] = getConsoleViewportSize();
    (void)console_height;
    bool hit_form_feed = false;
    for (size_t i = 0; i < page_height && current_line < lines.size();
         ++i, ++current_line) {
      safePrintLn(lines[current_line]);
      lines_shown++;

      if (!cfg.pause_form_feed &&
          lines[current_line].find('') != std::string::npos) {
        hit_form_feed = true;
        break;
      }

      if (!cfg.logical_lines) {
        size_t rows = estimate_screen_rows(lines[current_line], console_width);
        if (rows > 1) i += rows - 1;
      }
    }
    last_lines_shown = lines_shown;

    if (current_line >= lines.size()) {
      if (!cfg.exit_on_eof) {
        size_t eof_percent =
            lines.empty()
                ? 100
                : std::min<size_t>(100, current_line * 100 / lines.size());
        safePrint("--More--(");
        safePrint(std::to_string(eof_percent));
        safePrint("%) (END)");
        MoreCommand end_command = read_more_command(input_mode.input());
        safePrint("\r\033[K");
        if (end_command.action == MoreAction::Quit) {
          return {0, MoreAction::Quit};
        }
      }
      break;
    }

    if (hit_form_feed) {
      size_t ff_percent =
          lines.empty()
              ? 100
              : std::min<size_t>(100, current_line * 100 / lines.size());
      safePrint("--More--(");
      safePrint(std::to_string(ff_percent));
      safePrint("%)");
      MoreCommand ff_command = read_more_command(input_mode.input());
      safePrint("\r\033[K");
      if (ff_command.action == MoreAction::Quit) {
        return {0, MoreAction::Quit};
      }
      continue;
    }

    size_t percent =
        lines.empty()
            ? 100
            : std::min<size_t>(100, current_line * 100 / lines.size());
    if (cfg.help_prompt) {
      safePrint("--More--(");
      safePrint(std::to_string(percent));
      safePrint("%) [Press h for instructions]");
    } else {
      safePrint("--More--(");
      safePrint(std::to_string(percent));
      safePrint("%)");
    }

    MoreCommand command = read_more_command(input_mode.input());
    safePrint("\r\033[K");
    if (command.action == MoreAction::RepeatPrevious) {
      command = previous_command;
    } else if (command.action != MoreAction::None) {
      previous_command = command;
    }

    auto count_or = [&](size_t fallback) {
      return command.number == 0 ? fallback : command.number;
    };
    auto rewind_current_page = [&]() {
      current_line =
          current_line > last_lines_shown ? current_line - last_lines_shown : 0;
    };

    switch (command.action) {
      case MoreAction::Quit:
        return {0, MoreAction::Quit};
      case MoreAction::NextPage:
        lines_per_page = count_or(page_height);
        break;
      case MoreAction::SetPageSize:
        lines_per_page = count_or(page_height);
        break;
      case MoreAction::NextLine:
        lines_per_page = count_or(1);
        break;
      case MoreAction::Scroll:
        if (command.number != 0) scroll_len = command.number;
        lines_per_page = scroll_len;
        break;
      case MoreAction::SkipLines:
        current_line = std::min(lines.size(), current_line + count_or(1));
        lines_per_page = page_height;
        break;
      case MoreAction::SkipScreens:
        current_line =
            std::min(lines.size(), current_line + count_or(1) * page_height);
        lines_per_page = page_height;
        break;
      case MoreAction::PrevPage: {
        size_t pages = count_or(1);
        size_t rewind = last_lines_shown + pages * page_height;
        current_line = current_line > rewind ? current_line - rewind : 0;
        lines_per_page = page_height;
        break;
      }
      case MoreAction::Search:
        last_search = command.text;
        [[fallthrough]];
      case MoreAction::RepeatSearch: {
        if (last_search.empty()) break;
        size_t occurrences = count_or(1);
        size_t found = std::string::npos;
        for (size_t i = current_line; i < lines.size(); ++i) {
          if (lines[i].find(last_search) == std::string::npos) continue;
          if (--occurrences == 0) {
            found = i;
            break;
          }
        }
        if (found != std::string::npos) current_line = found;
        lines_per_page = page_height;
        break;
      }
      case MoreAction::DisplayLine:
        rewind_current_page();
        safePrint("line ");
        safePrintLn(std::to_string(current_line));
        lines_per_page = page_height;
        break;
      case MoreAction::DisplayFile:
        rewind_current_page();
        safePrint(filename.empty() ? "[stdin]" : filename);
        if (file_count > 1) {
          safePrint(" [");
          safePrint(std::to_string(file_index + 1));
          safePrint("/");
          safePrint(std::to_string(file_count));
          safePrint("]");
        }
        safePrint(" line ");
        safePrintLn(std::to_string(current_line));
        lines_per_page = page_height;
        break;
      case MoreAction::Help:
        rewind_current_page();
        print_runtime_help();
        lines_per_page = page_height;
        break;
      case MoreAction::ClearScreen:
        current_line = current_line > last_lines_shown
                           ? current_line - last_lines_shown
                           : 0;
        clear_console();
        lines_per_page = page_height;
        break;
      case MoreAction::NextFile:
      case MoreAction::PrevFile:
        return {0, command.action, command.number};
      case MoreAction::RepeatPrevious:
      case MoreAction::None:
        rewind_current_page();
        lines_per_page = page_height;
        break;
    }
  }

  return {0, MoreAction::NextFile, 1};
}

auto run(const Config& cfg) -> int {
  if (cfg.files.empty() && isInputConsole()) {
    safeErrorPrintLn("more: missing filename or piped input");
    safeErrorPrintLn("Usage: more [OPTION]... [FILE]...");
    return 1;
  }

  if (!isOutputConsole()) {
    bool needs_line_processing =
        cfg.squeeze_blank || cfg.start_line > 0 || !cfg.search_pattern.empty();
    if (cfg.files.empty()) {
      return needs_line_processing ? display_file_noninteractive("", cfg)
                                   : copy_file_to_stdout("");
    }
    int result = 0;
    for (const auto& file : cfg.files) {
      int file_result = needs_line_processing
                            ? display_file_noninteractive(file, cfg)
                            : copy_file_to_stdout(file);
      if (file_result != 0) {
        result = 1;
      }
    }
    return result;
  }
  if (cfg.files.empty()) {
    return display_file("", cfg, 0, 1).code;
  }

  int result = 0;
  size_t index = 0;
  while (index < cfg.files.size()) {
    MoreResult file_result =
        display_file(cfg.files[index], cfg, index, cfg.files.size());
    if (file_result.code != 0) result = 1;
    if (file_result.action == MoreAction::PrevFile) {
      size_t count = file_result.count == 0 ? 1 : file_result.count;
      index = index > count ? index - count : 0;
      continue;
    }
    if (file_result.action == MoreAction::NextFile) {
      size_t count = file_result.count == 0 ? 1 : file_result.count;
      index += count;
      continue;
    }
    break;
  }
  return result;
}

}  // namespace more_pipeline

REGISTER_COMMAND(
    more, "more", "more [OPTION]... [FILE]...",
    "Display the contents of a file, one screenful at a time.\n"
    "\n"
    "The more command is a filter for paging through text one screen at a "
    "time.\n"
    "\n"
    "Start modifiers:\n"
    "  +NUM       display file beginning from line number\n"
    "  +/PATTERN  display file beginning from pattern match\n"
    "\n"
    "Interactive commands:\n"
    "  SPACE/z     display next screenful; z also sets screen size\n"
    "  ENTER       display next line\n"
    "  d/s/f/b     scroll, skip lines, skip screens, or go back\n"
    "  / and n     search and repeat search\n"
    "  :n/:p/:f    next file, previous file, or file status\n"
    "  .           repeat previous command\n"
    "  h or ?      show interactive help\n"
    "  q           quit\n"
    "\n"
    "Note: On Windows, more uses the console for interactive paging.",
    "  more file.txt         display file one page at a time\n"
    "  more -s file.txt      squeeze blank lines\n"
    "  more -n 40 file.txt   use 40 lines per page\n"
    "  cat file | more       page through piped input",
    "less(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MORE_OPTIONS) {
  using namespace more_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"more");
    return 1;
  }

  return run(*cfg_result);
}
