/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
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
 *  - File: xargs.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for xargs.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief XARGS command options definition
 *
 * This array defines all the options supported by the xargs command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -n, --max-args: Use at most max-args arguments per command line
 * [IMPLEMENTED]
 * - @a -I: Replace occurrences of replace-str in the initial-arguments with
 * names [IMPLEMENTED]
 * - @a -i, --replace: Deprecated alias for -I [IMPLEMENTED]
 * - @a -0, --null: Input items are terminated by a null character [IMPLEMENTED]
 * - @a -d, --delimiter: Input items are terminated by the specified character
 * [IMPLEMENTED]
 * - @a -o, --open-tty: Reopen standard input as the console for children
 * [IMPLEMENTED ON WINDOWS CONSOLE]
 * - @a -t, --verbose: Print the command line on the standard error before
 * executing it [IMPLEMENTED]
 * - @a -p, --interactive: Prompt before running each command line
 * [IMPLEMENTED]
 * - @a -r, --no-run-if-empty: If the standard input does not contain any
 * nonblanks, do not run the command [IMPLEMENTED]
 * - @a -P, --max-procs: Run up to max-procs processes at a time [IMPLEMENTED]
 * - @a -s, --max-chars: Use at most max-chars chars per command line
 * [IMPLEMENTED]
 * - @a -x, --exit: Exit if the size (see -s) is exceeded [IMPLEMENTED]
 * - @a -L, --max-lines: Use at most max-lines nonblank input lines per
 * command line [IMPLEMENTED]
 * - @a -a, --arg-file: Read items from file instead of standard input
 * [IMPLEMENTED]
 * - @a -E: Set the logical EOF string [IMPLEMENTED]
 * - @a -e, --eof: Set or disable the logical EOF string [IMPLEMENTED]
 * - @a --show-limits: Display command-line length limits [IMPLEMENTED]
 * - @a --process-slot-var: Set an environment variable to the process slot
 * [IMPLEMENTED]
 */
auto constexpr XARGS_OPTIONS = std::array{
    OPTION("-n", "--max-args",
           "use at most max-args arguments per command line", INT_TYPE),
    OPTION("-I", "",
           "replace occurrences of replace-str in the initial-arguments with "
           "names",
           STRING_TYPE),
    OPTION("-i", "--replace",
           "deprecated alias for -I; replace occurrences of replace-str",
           OPTIONAL_STRING_TYPE),
    OPTION("-0", "--null", "input items are terminated by a null character"),
    OPTION("-d", "--delimiter",
           "input items are terminated by the specified character",
           STRING_TYPE),
    OPTION("-o", "--open-tty",
           "reopen standard input as the console in the child process"),
    OPTION("-t", "--verbose",
           "print the command line on the standard error before executing it"),
    OPTION("-p", "--interactive", "prompt before running each command line"),
    OPTION("-r", "--no-run-if-empty",
           "if the standard input does not contain any nonblanks, do not run "
           "the command"),
    OPTION("-P", "--max-procs", "run up to max-procs processes at a time",
           INT_TYPE),
    OPTION("-s", "--max-chars", "use at most max-chars chars per command line",
           INT_TYPE),
    OPTION("-x", "--exit", "exit if the size (see -s) is exceeded"),
    OPTION("-L", "--max-lines",
           "use at most max-lines nonblank input lines per command line",
           INT_TYPE),
    OPTION("-l", "",
           "deprecated alias for -L; use max-lines nonblank input lines",
           OPTIONAL_INT_TYPE),
    OPTION("-a", "--arg-file", "read items from file instead of standard input",
           STRING_TYPE),
    OPTION("-E", "", "set the logical EOF string", STRING_TYPE),
    OPTION("", "--eof", "set or disable the logical EOF string",
           OPTIONAL_STRING_TYPE),
    OPTION("-e", "", "deprecated alias for --eof", OPTIONAL_STRING_TYPE),
    OPTION("", "--show-limits", "display command-line length limits"),
    OPTION("", "--process-slot-var",
           "set an environment variable to the child process slot",
           STRING_TYPE)};

namespace xargs_pipeline {
namespace cp = core::pipeline;

template <size_t N>
auto option_present(const CommandContext<N> &ctx, std::string_view name)
    -> bool {
  if (!ctx.metas) return false;

  for (size_t i = 0; i < N; ++i) {
    if ((*ctx.metas)[i].long_name == name ||
        (*ctx.metas)[i].short_name == name) {
      return ctx.options.has(i);
    }
  }
  return false;
}

enum class BatchOptionFamily {
  None,
  MaxArgs,
  MaxLines,
  Replace,
};

struct BatchOptions {
  int max_args = 0;
  int max_lines = 0;
  std::string replace_str;
  BatchOptionFamily active_family = BatchOptionFamily::None;
  bool max_args_present = false;
  bool max_lines_present = false;
};

auto option_matches(const cmd::meta::OptionMeta &meta,
                    std::string_view short_name, std::string_view long_name)
    -> bool {
  return (!short_name.empty() && meta.short_name == short_name) ||
         (!long_name.empty() && meta.long_name == long_name);
}

auto warn_conflicting_batch_options() -> void {
  safeErrorPrint(
      "xargs: warning: options --replace, --max-lines and --max-args are "
      "mutually exclusive; using the last one\n");
}

auto normalize_batch_options(const CommandContext<XARGS_OPTIONS.size()> &ctx)
    -> BatchOptions {
  BatchOptions options;

  auto clear_active = [&]() {
    options.max_args = 0;
    options.max_lines = 0;
    options.replace_str.clear();
    options.max_args_present = false;
    options.max_lines_present = false;
  };

  auto switch_family = [&](BatchOptionFamily family) {
    if (options.active_family != BatchOptionFamily::None &&
        options.active_family != family) {
      warn_conflicting_batch_options();
      clear_active();
    }
    options.active_family = family;
  };

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= XARGS_OPTIONS.size()) continue;
    const auto &meta = (*ctx.metas)[occurrence.index];

    if (option_matches(meta, "-n", "--max-args")) {
      const auto *value = std::get_if<int>(&occurrence.value);
      if (!value) continue;

      if (options.active_family == BatchOptionFamily::Replace && *value == 1) {
        continue;
      }

      switch_family(BatchOptionFamily::MaxArgs);
      options.max_args = *value;
      options.max_args_present = true;
      continue;
    }

    if (option_matches(meta, "-L", "--max-lines") ||
        option_matches(meta, "-l", "")) {
      const auto *value = std::get_if<int>(&occurrence.value);
      if (!value) continue;

      switch_family(BatchOptionFamily::MaxLines);
      options.max_lines = *value < 0 ? 1 : *value;
      options.max_lines_present = true;
      continue;
    }

    if (option_matches(meta, "-I", "") ||
        option_matches(meta, "-i", "--replace")) {
      const auto *value = std::get_if<std::string>(&occurrence.value);
      if (!value) continue;

      switch_family(BatchOptionFamily::Replace);
      options.replace_str = *value;
      if (options.replace_str.empty() && !option_matches(meta, "-I", "")) {
        options.replace_str = "{}";
      }
    }
  }

  return options;
}

/**
 * @brief Parse arguments from stdin
 * @param delimiter Delimiter character (default: space/newline)
 * @param replace_str Replacement string for -I option
 * @return Vector of parsed arguments
 */
auto hex_value(char c) -> int {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

auto parse_delimiter(std::string_view text) -> cp::Result<char> {
  if (text.empty()) return std::unexpected("delimiter must not be empty");
  if (text.size() == 1) return text[0];
  if (text[0] != '\\') {
    return std::unexpected("delimiter must be a single character");
  }

  if (text.size() == 2) {
    switch (text[1]) {
      case '0':
        return '\0';
      case 'a':
        return '\a';
      case 'b':
        return '\b';
      case 'f':
        return '\f';
      case 'n':
        return '\n';
      case 'r':
        return '\r';
      case 't':
        return '\t';
      case 'v':
        return '\v';
      case '\\':
        return '\\';
      default:
        break;
    }
  }

  if (text.size() >= 3 && text[1] == 'x') {
    int value = 0;
    for (size_t i = 2; i < text.size(); ++i) {
      int digit = hex_value(text[i]);
      if (digit < 0) return std::unexpected("invalid hex delimiter escape");
      value = value * 16 + digit;
      if (value > 255) return std::unexpected("delimiter escape out of range");
    }
    return static_cast<char>(value);
  }

  if (text.size() >= 2 && text[1] >= '0' && text[1] <= '7') {
    int value = 0;
    for (size_t i = 1; i < text.size(); ++i) {
      if (text[i] < '0' || text[i] > '7') {
        return std::unexpected("invalid octal delimiter escape");
      }
      value = value * 8 + (text[i] - '0');
      if (value > 255) return std::unexpected("delimiter escape out of range");
    }
    return static_cast<char>(value);
  }

  return std::unexpected("delimiter must be a single character");
}

auto read_input_text(std::istream &input,
                     const std::optional<std::string> &eof_str) -> std::string {
  std::string text;
  if (!eof_str || eof_str->empty()) {
    text.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    return text;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == *eof_str) break;
    text += line;
    text.push_back('\n');
  }
  return text;
}

auto parse_default_arguments(std::string_view text)
    -> cp::Result<std::vector<std::string>> {
  SmallVector<std::string, 256> args;
  std::string arg;
  bool in_single_quote = false;
  bool in_double_quote = false;
  bool escaped = false;
  bool have_arg = false;

  auto flush_arg = [&]() {
    if (have_arg) {
      args.push_back(arg);
      arg.clear();
      have_arg = false;
    }
  };

  for (char c : text) {
    if (escaped) {
      arg += c;
      have_arg = true;
      escaped = false;
      continue;
    }

    if (in_single_quote) {
      if (c == '\'') {
        in_single_quote = false;
      } else {
        arg += c;
      }
      have_arg = true;
      continue;
    }

    if (in_double_quote) {
      if (c == '"') {
        in_double_quote = false;
      } else if (c == '\\') {
        escaped = true;
      } else {
        arg += c;
      }
      have_arg = true;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      have_arg = true;
    } else if (c == '\'') {
      in_single_quote = true;
      have_arg = true;
    } else if (c == '"') {
      in_double_quote = true;
      have_arg = true;
    } else if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
      flush_arg();
    } else {
      arg += c;
      have_arg = true;
    }
  }

  if (escaped) {
    return std::unexpected("unmatched backslash in input");
  }
  if (in_single_quote || in_double_quote) {
    return std::unexpected("unmatched quote in input");
  }

  flush_arg();
  return std::vector<std::string>(args.begin(), args.end());
}

auto split_blank_arguments(std::string_view line)
    -> cp::Result<std::vector<std::string>> {
  return parse_default_arguments(line);
}

auto parse_arguments(std::istream &input, char delimiter, bool split_blanks,
                     const std::optional<std::string> &eof_str)
    -> cp::Result<std::vector<std::string>> {
  auto text = read_input_text(input, eof_str);
  if (split_blanks) {
    return parse_default_arguments(text);
  }

  SmallVector<std::string, 256> args;
  std::string arg;

  for (char c : text) {
    bool is_separator = c == delimiter;

    if (is_separator) {
      if (!arg.empty()) {
        args.push_back(arg);
        arg.clear();
      }
    } else {
      arg += c;
    }
  }

  // Add last argument if not empty
  if (!arg.empty()) {
    args.push_back(arg);
  }

  return std::vector<std::string>(args.begin(), args.end());
}

auto parse_line_groups(std::istream &input, int max_lines,
                       const std::optional<std::string> &eof_str)
    -> cp::Result<std::vector<std::vector<std::string>>> {
  std::vector<std::vector<std::string>> groups;
  SmallVector<std::string, 256> current;
  int logical_lines = 0;
  bool continued_line = false;

  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (eof_str && !eof_str->empty() && line == *eof_str) break;

    bool ends_with_blank =
        !line.empty() && (line.back() == ' ' || line.back() == '\t');
    auto parsed_parts = split_blank_arguments(line);
    if (!parsed_parts) return std::unexpected(parsed_parts.error());
    auto parts = *parsed_parts;
    if (parts.empty() && !continued_line) continue;

    for (const auto &part : parts) current.push_back(part);

    if (ends_with_blank) {
      continued_line = true;
      continue;
    }

    if (!parts.empty() || continued_line) {
      ++logical_lines;
      continued_line = false;
    }

    if (logical_lines >= max_lines) {
      groups.emplace_back(current.begin(), current.end());
      current.clear();
      logical_lines = 0;
    }
  }

  if (continued_line && !current.empty()) ++logical_lines;
  if (!current.empty()) groups.emplace_back(current.begin(), current.end());

  return groups;
}

auto print_show_limits(int max_chars) -> void {
  constexpr int kWindowsCommandLineLimit = 32767;
  int buffer_size = max_chars > 0 ? max_chars : kWindowsCommandLineLimit;
  safeErrorPrint("POSIX upper limit on argument length (this system): ");
  safeErrorPrint(std::to_string(kWindowsCommandLineLimit));
  safeErrorPrint("\n");
  safeErrorPrint("Maximum length of command we could actually use: ");
  safeErrorPrint(std::to_string(kWindowsCommandLineLimit));
  safeErrorPrint("\n");
  safeErrorPrint("Size of command buffer we are actually using: ");
  safeErrorPrint(std::to_string(buffer_size));
  safeErrorPrint("\n");
}

auto materialize_arguments(const std::vector<std::string> &base_args,
                           const std::vector<std::string> &input_args,
                           const std::string &replace_str)
    -> std::vector<std::string> {
  SmallVector<std::string, 256> all_args;

  for (const auto &base_arg : base_args) {
    if (!replace_str.empty() &&
        base_arg.find(replace_str) != std::string::npos) {
      std::string replaced = base_arg;
      std::string replacement;
      for (size_t j = 0; j < input_args.size(); ++j) {
        if (j > 0) replacement += " ";
        replacement += input_args[j];
      }

      size_t pos = 0;
      while ((pos = replaced.find(replace_str, pos)) != std::string::npos) {
        replaced.replace(pos, replace_str.length(), replacement);
        pos += replacement.length();
      }
      all_args.push_back(replaced);
    } else {
      all_args.push_back(base_arg);
    }
  }

  if (replace_str.empty()) {
    for (const auto &input_arg : input_args) {
      all_args.push_back(input_arg);
    }
  }

  return std::vector<std::string>(all_args.begin(), all_args.end());
}

auto quote_arg(const std::wstring &arg) -> std::wstring {
  if (arg.empty()) return L"\"\"";

  bool need_quote = arg.find_first_of(L" \t\"") != std::wstring::npos;
  if (!need_quote) return arg;

  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      backslashes = 0;
      out.push_back(c);
    }
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

auto build_command_line(const std::string &command,
                        const std::vector<std::string> &args) -> std::wstring {
  std::wstring cmd_line = quote_arg(utf8_to_wstring(command));
  auto is_cmd_shell = [](std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char ch : value) {
      lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered == "cmd" || lowered == "cmd.exe";
  };
  auto is_cmd_c = [](std::string_view value) {
    return value == "/C" || value == "/c";
  };
  auto cmd_escape_arg = [](std::string_view arg) {
    std::string escaped;
    escaped.reserve(arg.size() * 2);
    for (char ch : arg) {
      switch (ch) {
        case ' ':
        case '\t':
        case '^':
        case '&':
        case '|':
        case '<':
        case '>':
        case '(':
        case ')':
        case '"':
          escaped.push_back('^');
          break;
        default:
          break;
      }
      escaped.push_back(ch);
    }
    return escaped;
  };

  bool cmd_c_tail = false;
  for (const auto &arg : args) {
    cmd_line.push_back(L' ');
    if (cmd_c_tail) {
      cmd_line += utf8_to_wstring(cmd_escape_arg(arg));
    } else {
      cmd_line += quote_arg(utf8_to_wstring(arg));
      if (is_cmd_shell(command) && is_cmd_c(arg)) {
        cmd_c_tail = true;
      }
    }
  }
  return cmd_line;
}

auto read_confirmation_line() -> std::string {
  std::string response;
  std::getline(std::cin, response);
  return response;
}

auto confirm_command(std::string_view command,
                     const std::vector<std::string> &args) -> bool {
  auto cmd_line = build_command_line(std::string(command), args);
  safeErrorPrint(wstring_to_utf8(cmd_line));
  safeErrorPrint(" ?...");

  auto response = read_confirmation_line();
  for (unsigned char ch : response) {
    if (std::isspace(ch)) continue;
    return ch == 'y' || ch == 'Y';
  }
  return false;
}

struct ChildProcess {
  PROCESS_INFORMATION pi{};
  DWORD exit_code = 0;
  int slot = 0;
};

enum class ChildStdinMode { Parent, NullDevice, Console };

struct ScopedHandle {
  HANDLE handle = INVALID_HANDLE_VALUE;

  ScopedHandle() = default;
  explicit ScopedHandle(HANDLE h) : handle(h) {}
  ScopedHandle(const ScopedHandle &) = delete;
  auto operator=(const ScopedHandle &) -> ScopedHandle & = delete;
  ScopedHandle(ScopedHandle &&other) noexcept : handle(other.handle) {
    other.handle = INVALID_HANDLE_VALUE;
  }
  auto operator=(ScopedHandle &&other) noexcept -> ScopedHandle & {
    if (this != &other) {
      reset();
      handle = other.handle;
      other.handle = INVALID_HANDLE_VALUE;
    }
    return *this;
  }
  ~ScopedHandle() { reset(); }

  auto valid() const -> bool {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
  }

  auto reset(HANDLE h = INVALID_HANDLE_VALUE) -> void {
    if (valid()) CloseHandle(handle);
    handle = h;
  }
};

auto make_inheritable_file_handle(const wchar_t *path) -> ScopedHandle {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  return ScopedHandle(h);
}

auto append_env_entry(std::vector<wchar_t> &block, std::wstring_view entry)
    -> void {
  block.insert(block.end(), entry.begin(), entry.end());
  block.push_back(L'\0');
}

auto build_environment_block(std::string_view slot_var, int slot)
    -> std::vector<wchar_t> {
  if (slot_var.empty()) return {};

  std::wstring name = utf8_to_wstring(std::string(slot_var));
  std::wstring prefix = name + L"=";
  std::vector<wchar_t> block;

  LPWCH env = GetEnvironmentStringsW();
  if (env) {
    for (const wchar_t *p = env; *p != L'\0';) {
      std::wstring_view entry(p);
      bool same_name =
          entry.size() >= prefix.size() &&
          _wcsnicmp(entry.data(), prefix.c_str(), prefix.size()) == 0;
      if (!same_name) append_env_entry(block, entry);
      p += entry.size() + 1;
    }
    FreeEnvironmentStringsW(env);
  }

  append_env_entry(block, prefix + std::to_wstring(slot));
  block.push_back(L'\0');
  return block;
}

auto launch_process(const std::string &command,
                    const std::vector<std::string> &args,
                    ChildStdinMode stdin_mode,
                    std::string_view process_slot_var, int slot)
    -> cp::Result<ChildProcess> {
  auto cmd_line = build_command_line(command, args);
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi{};
  ScopedHandle stdin_handle;

  if (stdin_mode == ChildStdinMode::NullDevice) {
    stdin_handle = make_inheritable_file_handle(L"NUL");
    if (!stdin_handle.valid()) {
      return std::unexpected("failed to open NUL for child stdin");
    }
  } else if (stdin_mode == ChildStdinMode::Console) {
    stdin_handle = make_inheritable_file_handle(L"CONIN$");
    if (!stdin_handle.valid()) {
      return std::unexpected("failed to open console input for child stdin");
    }
  }

  if (stdin_handle.valid()) {
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = stdin_handle.handle;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  }

  auto env_block = build_environment_block(process_slot_var, slot);
  LPVOID environment = env_block.empty() ? nullptr : env_block.data();

  BOOL success = CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr,
                                TRUE, CREATE_UNICODE_ENVIRONMENT, environment,
                                nullptr, &si, &pi);
  if (!success) {
    return std::unexpected("failed to execute command");
  }

  ChildProcess child;
  child.pi = pi;
  child.slot = slot;
  return child;
}

auto wait_child(ChildProcess &child) -> int {
  WaitForSingleObject(child.pi.hProcess, INFINITE);
  DWORD result = 0;
  GetExitCodeProcess(child.pi.hProcess, &result);
  CloseHandle(child.pi.hProcess);
  CloseHandle(child.pi.hThread);
  child.exit_code = result;
  return static_cast<int>(result);
}

auto map_child_exit_status(int child_status) -> int {
  if (child_status == 0) return 0;
  if (child_status == 255) return 124;
  if (child_status >= 1 && child_status <= 125) return 123;
  return child_status;
}

/**
 * @brief Execute command with arguments
 * @param command Command to execute
 * @param base_args Base arguments from command line
 * @param input_args Arguments from stdin
 * @param replace_str Replacement string
 * @param max_args Maximum arguments per execution
 * @param verbose Print command before execution
 * @return Exit code
 */
auto execute_command(const std::string &command,
                     const std::vector<std::string> &base_args,
                     const std::vector<std::string> &input_args,
                     const std::string &replace_str, int max_args,
                     int max_chars, bool exit_if_exceeded, int max_procs,
                     bool verbose, bool interactive, ChildStdinMode stdin_mode,
                     const std::string &process_slot_var) -> int {
  int exit_code = 0;
  bool stop_launching = false;

  if (max_args <= 0) {
    max_args = static_cast<int>(input_args.size());
  }
  if (!replace_str.empty()) max_args = 1;
  if (max_args <= 0) max_args = 1;
  if (max_procs <= 0) max_procs = 1;

  std::vector<ChildProcess> running;
  running.reserve(static_cast<size_t>(std::min(max_procs, 256)));
  SmallVector<int, 64> free_slots;
  int next_slot = 0;

  auto allocate_slot = [&]() -> int {
    if (!free_slots.empty()) {
      int slot = free_slots.back();
      free_slots.pop_back();
      return slot;
    }
    return next_slot++;
  };

  auto wait_one = [&]() {
    if (running.empty()) return;
    int child_status = wait_child(running.front());
    if (child_status != 0) {
      exit_code = map_child_exit_status(child_status);
      if (child_status == 255) stop_launching = true;
    }
    if (!process_slot_var.empty()) free_slots.push_back(running.front().slot);
    running.erase(running.begin());
  };

  SmallVector<std::string, 256> batch;
  batch.reserve(static_cast<size_t>(std::max(max_args, 1)));

  auto fits_limits = [&](const std::vector<std::string> &candidate) -> bool {
    if (max_args > 0 && static_cast<int>(candidate.size()) > max_args) {
      return false;
    }
    if (max_chars > 0) {
      auto materialized =
          materialize_arguments(base_args, candidate, replace_str);
      auto cmd_line = build_command_line(command, materialized);
      if (cmd_line.size() > static_cast<size_t>(max_chars)) {
        return false;
      }
    }
    return true;
  };

  auto run_batch = [&](const std::vector<std::string> &current_batch,
                       bool allow_empty = false) -> bool {
    if (stop_launching) return false;
    if (current_batch.empty() && !allow_empty) return true;

    auto all_args =
        materialize_arguments(base_args, current_batch, replace_str);
    if (interactive && !confirm_command(command, all_args)) {
      return true;
    }
    if (verbose) {
      safeErrorPrint(command);
      for (const auto &arg : all_args) {
        safeErrorPrint(" ");
        safeErrorPrint(arg);
      }
      safeErrorPrint("\n");
    }

    int slot = process_slot_var.empty() ? 0 : allocate_slot();
    auto child =
        launch_process(command, all_args, stdin_mode, process_slot_var, slot);
    if (!child) {
      safeErrorPrint("xargs: failed to execute '");
      safeErrorPrint(command);
      safeErrorPrint("'\n");
      if (!process_slot_var.empty()) free_slots.push_back(slot);
      exit_code = 127;
      stop_launching = true;
      return false;
    }

    running.push_back(*child);
    if (running.size() >= static_cast<size_t>(max_procs)) {
      wait_one();
      if (stop_launching) return false;
    }
    return true;
  };

  if (input_args.empty()) {
    if (!fits_limits({})) {
      if (exit_if_exceeded) {
        cp::report_custom_error(L"xargs", L"command line length exceeded");
        return 1;
      }
    }
    run_batch({}, true);
    while (!running.empty()) wait_one();
    return exit_code;
  }

  auto flush_batch = [&]() -> bool {
    if (batch.empty()) return true;
    bool ok = run_batch(std::vector<std::string>(batch.begin(), batch.end()));
    batch.clear();
    return ok;
  };

  for (const auto &input_arg : input_args) {
    if (stop_launching) break;
    batch.push_back(input_arg);
    if (fits_limits(std::vector<std::string>(batch.begin(), batch.end()))) {
      continue;
    }

    batch.pop_back();
    if (!flush_batch()) {
      return exit_code == 0 ? 1 : exit_code;
    }

    batch.push_back(input_arg);
    auto current_batch = std::vector<std::string>(batch.begin(), batch.end());
    if (!fits_limits(current_batch)) {
      if (exit_if_exceeded) {
        cp::report_custom_error(L"xargs", L"command line length exceeded");
        return 1;
      }
    }
  }

  if (!flush_batch()) {
    return exit_code == 0 ? 1 : exit_code;
  }

  while (!running.empty()) wait_one();
  return exit_code;
}

}  // namespace xargs_pipeline

REGISTER_COMMAND(
    xargs, "xargs", "build and execute command lines from standard input",
    "Build and execute command lines from standard input.\n"
    "\n"
    "Items are separated by blanks. The result command line is executed\n"
    "after each group of max-args items is read.",
    "  find . -name '*.cpp' | xargs rm -f     Delete all cpp files\n"
    "  echo file1 file2 | xargs cat         Concatenate files\n"
    "  find . -name '*.txt' | xargs -n1 grep 'pattern'  Search one file at a "
    "time",
    "find(1), grep(1), sed(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    XARGS_OPTIONS) {
  using namespace xargs_pipeline;

  bool use_null = ctx.get<bool>("-0", false) || ctx.get<bool>("--null", false);
  bool verbose =
      ctx.get<bool>("-t", false) || ctx.get<bool>("--verbose", false);
  bool interactive =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--interactive", false);
  bool no_run_if_empty =
      ctx.get<bool>("-r", false) || ctx.get<bool>("--no-run-if-empty", false);
  auto batch_options = normalize_batch_options(ctx);
  int max_args = batch_options.max_args;
  int max_lines = batch_options.max_lines;
  std::string replace_str = batch_options.replace_str;
  int max_procs = ctx.get<int>("--max-procs", 1);
  if (max_procs == 1) max_procs = ctx.get<int>("-P", 1);
  int max_chars = ctx.get<int>("--max-chars", 0);
  if (max_chars == 0) max_chars = ctx.get<int>("-s", 0);
  bool exit_if_exceeded =
      ctx.get<bool>("--exit", false) || ctx.get<bool>("-x", false);
  bool show_limits = ctx.get<bool>("--show-limits", false);
  if (max_args < 0 || (batch_options.max_args_present && max_args == 0)) {
    cp::report_custom_error(L"xargs", L"max-args must be positive");
    return 1;
  }
  if (max_procs < 0) {
    cp::report_custom_error(L"xargs", L"max-procs must be non-negative");
    return 1;
  }
  if (max_procs == 0) max_procs = std::numeric_limits<int>::max();
  if (max_chars < 0) {
    cp::report_custom_error(L"xargs", L"max-chars must be non-negative");
    return 1;
  }
  if (max_lines < 0 || (batch_options.max_lines_present && max_lines == 0)) {
    cp::report_custom_error(L"xargs", L"max-lines must be positive");
    return 1;
  }
  std::string delimiter_arg = ctx.get<std::string>("--delimiter", "");
  if (delimiter_arg.empty()) delimiter_arg = ctx.get<std::string>("-d", "");
  std::string arg_file = ctx.get<std::string>("--arg-file", "");
  if (arg_file.empty()) arg_file = ctx.get<std::string>("-a", "");
  std::string eof_arg = ctx.get<std::string>("-E", "");
  bool eof_enabled = ctx.has("-E");
  if (!eof_enabled && ctx.has("--eof")) {
    eof_arg = ctx.get<std::string>("--eof", "");
    eof_enabled = !eof_arg.empty();
  }
  if (!eof_enabled && ctx.has("-e")) {
    eof_arg = ctx.get<std::string>("-e", "");
    eof_enabled = !eof_arg.empty();
  }
  std::string process_slot_var = ctx.get<std::string>("--process-slot-var", "");
  bool open_tty =
      ctx.get<bool>("-o", false) || ctx.get<bool>("--open-tty", false);

  if (option_present(ctx, "--process-slot-var") && process_slot_var.empty()) {
    cp::report_custom_error(L"xargs", L"process-slot-var must not be empty");
    return 1;
  }
  if (process_slot_var.find('=') != std::string::npos) {
    cp::report_custom_error(L"xargs", L"process-slot-var must not contain '='");
    return 1;
  }

  if (!replace_str.empty() || max_lines > 0 || !delimiter_arg.empty()) {
    exit_if_exceeded = true;
  }

  if (show_limits) print_show_limits(max_chars);

  char delimiter = use_null ? '\0' : ' ';
  bool split_blanks = !use_null && replace_str.empty();
  if (!delimiter_arg.empty()) {
    auto parsed_delim = parse_delimiter(delimiter_arg);
    if (!parsed_delim) {
      cp::report_error(parsed_delim, L"xargs");
      return 1;
    }
    delimiter = *parsed_delim;
    split_blanks = false;
  } else if (!replace_str.empty() && !use_null) {
    delimiter = '\n';
    split_blanks = false;
  }

  std::ifstream arg_input;
  std::istream *input = &std::cin;
  if (!arg_file.empty()) {
    arg_input.open(arg_file, std::ios::binary);
    if (!arg_input.is_open()) {
      safeErrorPrint("xargs: cannot open '");
      safeErrorPrint(arg_file);
      safeErrorPrint("'\n");
      return 1;
    }
    input = &arg_input;
  }

  std::optional<std::string> logical_eof;
  if (!use_null && delimiter_arg.empty() && eof_enabled) {
    logical_eof = eof_arg;
  }

  std::vector<std::vector<std::string>> input_groups;
  bool use_line_groups = max_lines > 0 && !use_null && delimiter_arg.empty() &&
                         replace_str.empty();
  if (use_line_groups) {
    auto parsed_groups = parse_line_groups(*input, max_lines, logical_eof);
    if (!parsed_groups) {
      cp::report_error(parsed_groups, L"xargs");
      return 1;
    }
    input_groups = *parsed_groups;
  } else {
    auto input_args_vec =
        parse_arguments(*input, delimiter, split_blanks, logical_eof);
    if (!input_args_vec) {
      cp::report_error(input_args_vec, L"xargs");
      return 1;
    }
    input_groups.emplace_back(input_args_vec->begin(), input_args_vec->end());
  }

  bool input_empty = true;
  for (const auto &group : input_groups) {
    if (!group.empty()) {
      input_empty = false;
      break;
    }
  }

  // Get command to execute (first positional arg)
  if (ctx.positionals.empty()) {
    // Default to echo if no command specified
    if (no_run_if_empty && input_empty) {
      return 0;
    }

    if (input_groups.empty()) input_groups.emplace_back();

    for (const auto &input_args : input_groups) {
      size_t chunk_size =
          max_args > 0 ? static_cast<size_t>(max_args) : input_args.size();
      if (chunk_size == 0) chunk_size = 1;

      for (size_t begin = 0; begin <= input_args.size(); begin += chunk_size) {
        size_t end = std::min(begin + chunk_size, input_args.size());
        if (begin == input_args.size() && !input_args.empty()) break;

        if (verbose) {
          safeErrorPrint("echo");
          for (size_t i = begin; i < end; ++i) {
            safeErrorPrint(" ");
            safeErrorPrint(input_args[i]);
          }
          safeErrorPrint("\n");
        }

        std::vector<std::string> echo_args;
        echo_args.reserve(end - begin);
        for (size_t i = begin; i < end; ++i) {
          echo_args.push_back(input_args[i]);
        }
        if (interactive && !confirm_command("echo", echo_args)) {
          if (input_args.empty()) break;
          continue;
        }

        for (size_t i = begin; i < end; ++i) {
          if (i > begin) safePrint(" ");
          safePrint(input_args[i]);
        }
        safePrint("\n");

        if (input_args.empty()) break;
      }
    }
    return 0;
  }

  std::string command = std::string(ctx.positionals[0]);
  SmallVector<std::string, 32> base_args;

  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    base_args.push_back(std::string(ctx.positionals[i]));
  }

  // If no input arguments, check if we should skip execution
  if (input_empty) {
    // Skip if -r (no-run-if-empty) is specified
    if (no_run_if_empty) {
      return 0;
    }
    // Skip if -I is specified but there's nothing to replace
    if (!replace_str.empty()) {
      return 0;
    }
    if (input_groups.empty()) input_groups.emplace_back();
  }

  ChildStdinMode stdin_mode = ChildStdinMode::NullDevice;
  if (open_tty) {
    stdin_mode = ChildStdinMode::Console;
  } else if (!arg_file.empty()) {
    stdin_mode = ChildStdinMode::Parent;
  }

  // Execute command with arguments - convert SmallVector to std::vector for
  // compatibility
  std::vector<std::string> base_args_vec(base_args.begin(), base_args.end());
  int exit_code = 0;
  for (const auto &input_args : input_groups) {
    int status =
        execute_command(command, base_args_vec, input_args, replace_str,
                        max_args, max_chars, exit_if_exceeded, max_procs,
                        verbose, interactive, stdin_mode, process_slot_var);
    if (status != 0) exit_code = status;
    if (status == 124 || status == 126 || status == 127) return status;
  }
  return exit_code;
}
