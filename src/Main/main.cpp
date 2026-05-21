/*
 *  Copyright (c) 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: main.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
// src/main.cpp
// Main entry point for WinuxCmd
import std;
import core;
import utils;
import readline;
import native_completion;
import version;

namespace {
static std::string g_repl_executable_path;
enum class FallbackShell { Cmd, PowerShell };
static FallbackShell g_repl_fallback_shell = FallbackShell::Cmd;
static std::wstring g_repl_powershell_exe = L"powershell.exe";

static std::string toLowerAscii(std::string s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static bool startsWithCaseInsensitive(std::string_view text,
                                      std::string_view prefix) {
  if (prefix.size() > text.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    unsigned char a = static_cast<unsigned char>(text[i]);
    unsigned char b = static_cast<unsigned char>(prefix[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

static std::vector<CompletionItem> getCommandCompletions(
    std::string_view prefix) {
  std::vector<CompletionItem> items;
  auto all = CommandRegistry::getAllCommands();
  items.reserve(all.size() + 64);

  std::unordered_set<std::string> seen;
  seen.reserve(all.size() * 2 + 128);

  for (auto &[name, desc] : all) {
    if (!startsWithCaseInsensitive(name, prefix)) continue;
    std::string key(name);
    if (seen.insert(toLowerAscii(key)).second)
      items.push_back({std::move(key), std::string(desc)});
  }

  bool includePowerShell = (g_repl_fallback_shell == FallbackShell::PowerShell);
  auto native =
      queryNativeCommandCompletionsForShell(prefix, includePowerShell);
  for (const auto &item : native) {
    if (seen.insert(toLowerAscii(item.text)).second)
      items.push_back({item.text, item.hint});
  }

  std::ranges::sort(items, {}, &CompletionItem::text);
  return items;
}

static std::vector<CompletionItem> getWindowsOptionCompletions(
    std::string_view cmd_name, std::string_view prefix) {
  std::vector<CompletionItem> items;
  bool includePowerShell = (g_repl_fallback_shell == FallbackShell::PowerShell);
  auto native =
      queryNativeOptionCompletionsForShell(cmd_name, prefix, includePowerShell);
  items.reserve(native.size());
  for (const auto &item : native) {
    items.push_back({item.text, item.hint});
  }
  return items;
}

static std::vector<CompletionItem> getPathCompletions(
    std::string_view token_prefix) {
  std::vector<CompletionItem> items;
  std::string prefix(token_prefix);

  std::filesystem::path base_dir = ".";
  std::string name_prefix = prefix;
  bool has_dir_part = false;

  // Support both '/' and '\' separators on Windows input.
  size_t slash_pos = prefix.find_last_of("/\\");
  if (slash_pos != std::string::npos) {
    has_dir_part = true;
    std::string dir_part = prefix.substr(0, slash_pos + 1);
    name_prefix = prefix.substr(slash_pos + 1);
    std::error_code ec;
    std::filesystem::path p = std::filesystem::path(dir_part);
    if (p.empty())
      base_dir = ".";
    else
      base_dir = p;
    if (!std::filesystem::exists(base_dir, ec) || ec) return items;
  }

  std::error_code ec;
  if (!std::filesystem::exists(base_dir, ec) || ec) return items;
  if (!std::filesystem::is_directory(base_dir, ec) || ec) return items;

  for (const auto &entry : std::filesystem::directory_iterator(
           base_dir, std::filesystem::directory_options::skip_permission_denied,
           ec)) {
    if (ec) break;
    auto name = entry.path().filename().string();
    if (!startsWithCaseInsensitive(name, name_prefix)) continue;

    std::string candidate;
    if (has_dir_part) {
      candidate = prefix.substr(0, slash_pos + 1) + name;
    } else {
      candidate = name;
    }
    bool is_dir = entry.is_directory(ec) && !ec;
    if (is_dir) candidate += "\\";
    items.push_back({candidate, is_dir ? "Directory" : "File"});
  }

  std::ranges::sort(items, {}, &CompletionItem::text);
  return items;
}

}  // namespace

/**
 * @brief Print help information
 * @return Exit code (1 - error)
 */
static int printHelp() noexcept {
  safePrintLn(L"WinuxCmd - Windows Compatible Linux Command Set");
  safePrintLn(L"Usage: winuxcmd <command> [options]...");
  safePrintLn(L"");
  safePrintLn(L"Available commands:");

  // Get all registered commands and display them with brief descriptions
  auto commands = CommandRegistry::getAllCommands();
  for (const auto &[cmd_name, cmd_desc] : commands) {
    // Display command name with its brief description
    std::wstring cmd_str = utf8_to_wstring(std::string(cmd_name));
    std::wstring desc_str = utf8_to_wstring(std::string(cmd_desc));
    // Pad command name for alignment
    if (cmd_str.length() < 10) {
      cmd_str.append(10 - cmd_str.length(), L' ');
    }
    safePrintLn(L"  " + cmd_str + L"   " + desc_str);
  }

  safePrintLn(L"");
  safePrintLn(L"Use 'winuxcmd <command> --help' for command-specific help.");
  return 1;
}

static std::wstring buildReplPrompt() noexcept {
  try {
    auto cwd = std::filesystem::current_path().wstring();
    return L"winux " + cwd + L"> ";
  } catch (...) {
    return L"winux > ";
  }
}

static std::string toPowerShellEncodedCommand(const std::wstring &script) {
  // PowerShell -EncodedCommand expects UTF-16LE bytes.
  std::vector<unsigned char> bytes;
  bytes.reserve(script.size() * 2);
  for (wchar_t ch : script) {
    auto u = static_cast<unsigned int>(ch);
    bytes.push_back(static_cast<unsigned char>(u & 0xFF));
    bytes.push_back(static_cast<unsigned char>((u >> 8) & 0xFF));
  }
  return encoding::base64_encode(bytes);
}

static bool isPowerShellProcessName(std::wstring_view name) {
  std::wstring lower(name);
  std::ranges::transform(lower, lower.begin(), [](wchar_t c) {
    return static_cast<wchar_t>(std::towlower(c));
  });
  return lower == L"powershell.exe" || lower == L"pwsh.exe";
}

static std::optional<std::wstring> getProcessExeNameByPid(DWORD pid) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return std::nullopt;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (!Process32FirstW(snap, &pe)) {
    CloseHandle(snap);
    return std::nullopt;
  }
  do {
    if (pe.th32ProcessID == pid) {
      std::wstring name = pe.szExeFile;
      CloseHandle(snap);
      return name;
    }
  } while (Process32NextW(snap, &pe));
  CloseHandle(snap);
  return std::nullopt;
}

static std::optional<DWORD> getParentPid(DWORD pid) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return std::nullopt;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (!Process32FirstW(snap, &pe)) {
    CloseHandle(snap);
    return std::nullopt;
  }
  do {
    if (pe.th32ProcessID == pid) {
      DWORD ppid = pe.th32ParentProcessID;
      CloseHandle(snap);
      return ppid;
    }
  } while (Process32NextW(snap, &pe));
  CloseHandle(snap);
  return std::nullopt;
}

static void detectReplFallbackShell() {
  g_repl_fallback_shell = FallbackShell::Cmd;
  g_repl_powershell_exe = L"powershell.exe";

  auto maybe_parent_pid = getParentPid(GetCurrentProcessId());
  if (!maybe_parent_pid.has_value()) return;
  auto maybe_parent_name = getProcessExeNameByPid(*maybe_parent_pid);
  if (!maybe_parent_name.has_value()) return;
  if (!isPowerShellProcessName(*maybe_parent_name)) return;

  g_repl_fallback_shell = FallbackShell::PowerShell;
  std::wstring lowered = *maybe_parent_name;
  std::ranges::transform(lowered, lowered.begin(), [](wchar_t c) {
    return static_cast<wchar_t>(std::towlower(c));
  });
  if (lowered == L"pwsh.exe") {
    g_repl_powershell_exe = L"pwsh.exe";
  } else {
    g_repl_powershell_exe = L"powershell.exe";
  }
}

static int runNativeFallback(const std::string &line) noexcept {
  if (line.empty()) return 0;
  std::wstring cmdline;
  if (g_repl_fallback_shell == FallbackShell::PowerShell) {
    std::wstring script = utf8_to_wstring(line);
    std::string encoded = toPowerShellEncodedCommand(script);
    cmdline =
        g_repl_powershell_exe +
        L" -NoLogo -NoProfile -ExecutionPolicy RemoteSigned -EncodedCommand " +
        utf8_to_wstring(encoded);
  } else {
    cmdline = L"cmd.exe /d /c " + utf8_to_wstring(line);
  }

  STARTUPINFOW si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);

  // CreateProcessW requires a mutable command buffer.
  std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
  mutable_cmd.push_back(L'\0');

  BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                           0, nullptr, nullptr, &si, &pi);
  if (!ok) {
    safePrintLn(L"winuxcmd: failed to run native command: " +
                utf8_to_wstring(line));
    return 127;
  }

  // While waiting for native child process, ignore Ctrl+C in parent so only
  // the child (e.g. ping) handles the interrupt and winuxcmd REPL survives.
  SetConsoleCtrlHandler(nullptr, TRUE);
  WaitForSingleObject(pi.hProcess, INFINITE);
  SetConsoleCtrlHandler(nullptr, FALSE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return static_cast<int>(exit_code);
}

static std::string trimAscii(std::string s) {
  auto is_ws = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
    s.erase(s.begin());
  while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

static bool hasShellMeta(std::string_view line) {
  return line.find('|') != std::string_view::npos ||
         line.find('>') != std::string_view::npos ||
         line.find('<') != std::string_view::npos ||
         line.find('&') != std::string_view::npos;
}

static std::optional<std::string> rewriteSudoBuiltinLine(
    const std::string &line) {
  std::istringstream iss(line);
  std::string first;
  std::string second;
  if (!(iss >> first)) return std::nullopt;
  if (toLowerAscii(first) != "sudo") return std::nullopt;
  if (!(iss >> second)) return std::nullopt;
  if (g_repl_executable_path.empty()) return std::nullopt;

  if (!CommandRegistry::hasCommand(second)) {
    auto lowered = toLowerAscii(second);
    if (!CommandRegistry::hasCommand(lowered)) return std::nullopt;
    second = std::move(lowered);
  }

  size_t rest_start = line.find_first_not_of(" \t", first.size());
  if (rest_start == std::string::npos) return std::nullopt;
  std::string rest = line.substr(rest_start);

  auto exe = g_repl_executable_path;
  if (exe.find(' ') != std::string::npos ||
      exe.find('\t') != std::string::npos) {
    exe = "\"" + exe + "\"";
  }

  // Use explicit winuxcmd.exe path so native Windows sudo can elevate and
  // execute built-in commands that do not exist as standalone binaries.
  std::string rewritten = "sudo " + exe + " " + rest;
  return rewritten;
}

static std::string rewritePipeBuiltinsLine(const std::string &line) {
  if (line.find('|') == std::string::npos || g_repl_executable_path.empty()) {
    return line;
  }

  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= line.size()) {
    size_t pos = line.find('|', start);
    if (pos == std::string::npos) {
      parts.push_back(line.substr(start));
      break;
    }
    parts.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }

  for (auto &part : parts) {
    auto trimmed = trimAscii(part);
    if (trimmed.empty()) continue;

    std::istringstream iss(trimmed);
    std::string first;
    if (!(iss >> first)) continue;

    std::string lowered = toLowerAscii(first);
    if (!CommandRegistry::hasCommand(first) &&
        !CommandRegistry::hasCommand(lowered)) {
      continue;
    }

    auto exe = g_repl_executable_path;
    if (exe.find(' ') != std::string::npos ||
        exe.find('\t') != std::string::npos) {
      exe = "\"" + exe + "\"";
    }
    part = exe + " " + trimmed;
  }

  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out += " | ";
    out += trimAscii(parts[i]);
  }
  return out;
}

// -----------------------------------------------------------------------------
// Interactive REPL
// -----------------------------------------------------------------------------

/// Build a completer function that suggests command names (prefix match) and
/// command options (after a space).
static Completer makeCompleter() {
  return [](std::string_view input) -> std::vector<CompletionItem> {
    // Complete only the segment after the last pipe.
    std::string full_input(input);
    size_t seg_start = 0;
    if (auto pipe_pos = full_input.rfind('|'); pipe_pos != std::string::npos) {
      seg_start = pipe_pos + 1;
      while (seg_start < full_input.size() && full_input[seg_start] == ' ')
        ++seg_start;
    }

    std::string prefix_before = full_input.substr(0, seg_start);
    std::string segment = full_input.substr(seg_start);
    auto space = segment.find(' ');

    if (space == std::string::npos) {
      auto items = getCommandCompletions(segment);
      for (auto &item : items) item.text = prefix_before + item.text;
      return items;
    }

    std::string cmd_name = segment.substr(0, space);
    std::string rest = segment.substr(space + 1);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());

    size_t token_start_in_rest = 0;
    if (auto pos = rest.find_last_of(" \t"); pos != std::string::npos)
      token_start_in_rest = pos + 1;
    std::string current_token = rest.substr(token_start_in_rest);

    auto buildFullText = [&](std::string_view replacement) {
      std::string replaced = segment.substr(0, space + 1);
      replaced += rest.substr(0, token_start_in_rest);
      replaced += replacement;
      return prefix_before + replaced;
    };

    std::vector<CompletionItem> items;
    std::unordered_set<std::string> seen;
    auto addItem = [&](std::string text, std::string hint) {
      std::string key = toLowerAscii(text);
      if (seen.insert(std::move(key)).second)
        items.push_back({std::move(text), std::move(hint)});
    };

    auto opts = CommandRegistry::getCommandOptions(cmd_name);
    for (auto &opt : opts) {
      for (const std::string *form : {&opt.short_name, &opt.long_name}) {
        if (form->empty()) continue;
        if (current_token.empty() || form->starts_with(current_token)) {
          addItem(buildFullText(*form), opt.description);
        }
      }
    }

    auto nativeOpts = getWindowsOptionCompletions(cmd_name, current_token);
    for (auto &opt : nativeOpts) {
      addItem(buildFullText(opt.text), opt.hint);
    }

    // File/dir completion should not be overshadowed by option completion.
    auto pathItems = getPathCompletions(current_token);
    for (auto &p : pathItems) {
      addItem(buildFullText(p.text), p.hint);
    }

    std::ranges::sort(items, {}, &CompletionItem::text);
    return items;
  };
}
/// Run WinuxCmd in interactive REPL mode.
static void runReplMode() noexcept {
  safePrintLn(
      L"WinuxCmd " + utf8_to_wstring(WinuxCmd::VERSION_STRING) +
      L"  (interactive)  Type 'exit' to quit, '--help' for command list.");
  safePrintLn(
      L"Use Tab for completions; \u2191\u2193 for history; \u2190\u2192 to "
      L"move cursor.\n");

  auto completer = makeCompleter();

  while (true) {
    std::string line = readInteractiveLine(buildReplPrompt(), completer);

    // Ctrl+D sentinel
    if (!line.empty() && line[0] == '\x04') break;

    // Strip leading/trailing whitespace
    auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) continue;
    line = line.substr(first);
    auto last = line.find_last_not_of(" \t\r\n");
    if (last != std::string::npos) line = line.substr(0, last + 1);
    if (line.empty()) continue;

    if (line == "exit" || line == "quit") break;

    std::string resolved_line = line;
    if (auto rewritten = rewriteSudoBuiltinLine(resolved_line);
        rewritten.has_value()) {
      resolved_line = *rewritten;
    }

    // REPL does not implement a full shell parser.
    // Route shell-style compound commands to native-shell fallback.
    if (hasShellMeta(resolved_line)) {
      runNativeFallback(rewritePipeBuiltinsLine(resolved_line));
      continue;
    }

    // Tokenise with basic quoting support
    // Quoted arguments get a \x01 prefix to protect wildcards from expansion
    std::vector<std::string> tokens;
    {
      std::string tok;
      bool in_single_quote = false;
      bool in_double_quote = false;
      bool was_quoted = false;
      for (size_t i = 0; i < resolved_line.size(); ++i) {
        char c = resolved_line[i];
        if (c == '\'' && !in_double_quote) {
          in_single_quote = !in_single_quote;
          was_quoted = true;
        } else if (c == '"' && !in_single_quote) {
          in_double_quote = !in_double_quote;
          was_quoted = true;
        } else if (std::isspace(static_cast<unsigned char>(c)) &&
                   !in_single_quote && !in_double_quote) {
          if (!tok.empty()) {
            if (was_quoted) {
              tokens.push_back("\x01" + tok);
            } else {
              tokens.push_back(std::move(tok));
            }
            tok.clear();
            was_quoted = false;
          }
        } else {
          tok += c;
        }
      }
      if (!tok.empty()) {
        if (was_quoted) {
          tokens.push_back("\x01" + tok);
        } else {
          tokens.push_back(std::move(tok));
        }
      }
    }
    if (tokens.empty()) continue;

    // REPL built-ins: keep cwd changes in current winuxcmd process.
    std::string lower_cmd = toLowerAscii(tokens[0]);
    if (lower_cmd == "cd" || lower_cmd == "chdir") {
      std::string arg = line.substr(tokens[0].size());
      arg = trimAscii(arg);
      if (arg.empty()) {
        safePrintLn(std::filesystem::current_path().wstring());
        continue;
      }
      if (arg.size() >= 2 && ((arg.front() == '"' && arg.back() == '"') ||
                              (arg.front() == '\'' && arg.back() == '\''))) {
        arg = arg.substr(1, arg.size() - 2);
      }
      std::wstring warg = utf8_to_wstring(arg);
      if (!SetCurrentDirectoryW(warg.c_str())) {
        safePrintLn(L"cd: cannot change directory: " + utf8_to_wstring(arg));
      }
      continue;
    }

    if (tokens[0].size() == 2 &&
        std::isalpha(static_cast<unsigned char>(tokens[0][0])) &&
        tokens[0][1] == ':') {
      std::wstring wdrive = utf8_to_wstring(tokens[0]);
      if (!SetCurrentDirectoryW(wdrive.c_str())) {
        safePrintLn(L"cd: cannot change drive: " + wdrive);
      }
      continue;
    }

    std::vector<std::string_view> args;
    args.reserve(tokens.size());
    for (auto &t : tokens) args.emplace_back(t);

    std::string_view cmd_name = args[0];
    std::span<std::string_view> cmd_args(args.data() + 1, args.size() - 1);
    if (CommandRegistry::hasCommand(cmd_name)) {
      CommandRegistry::dispatch(cmd_name, cmd_args);
    } else {
      runNativeFallback(resolved_line);
    }
  }

  safePrintLn(L"\nGoodbye!");
}

/**
 * @brief Main function for WinuxCmd
 * @param argc Number of command-line arguments
 * @param argv Array of command-line arguments
 * @return Exit code from the executed command (0 = success, non-zero = error)
 */
int main(int argc, char *argv[]) noexcept {
  if (argc < 1) {
    return printHelp();
  }
  // Automatically set console or pipe output.
  setupConsoleForUnicode();
  detectReplFallbackShell();
  // Get the executable name (stem only)
  std::string self_name = path::get_executable_name(argv[0]);

  // Convert command-line arguments to string_views for efficiency
  std::vector<std::string_view> args;
  args.reserve(argc - 1);
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  if (self_name == "winuxcmd") {
    // Mode 1: winuxcmd <command> [args...] (e.g., winuxcmd ls -la)
    if (args.empty()) {
      // Enter interactive REPL when running on a real console, otherwise help.
      HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
      DWORD mode = 0;
      if (isOutputConsole() && GetConsoleMode(hIn, &mode)) {
        g_repl_executable_path = path::get_executable_path(argv[0]);
        runReplMode();
        return 0;
      }
      return printHelp();
    }

    // Machine-readable completion query: winuxcmd --complete-cmd [prefix]
    if (args[0] == "--complete-cmd") {
      std::string_view prefix = args.size() >= 2 ? args[1] : "";
      auto items = getCommandCompletions(prefix);
      for (const auto &item : items) {
        safePrint(item.text);
        safePrint("\t");
        safePrintLn(item.hint);
      }
      return 0;
    }

    // Machine-readable option query: winuxcmd --complete-opt <cmd> [prefix]
    if (args[0] == "--complete-opt" && args.size() >= 2) {
      std::string_view cmd = args[1];
      std::string_view prefix = args.size() >= 3 ? args[2] : "";
      auto opts = CommandRegistry::getCommandOptions(cmd);
      for (auto &opt : opts) {
        for (const std::string *form : {&opt.short_name, &opt.long_name}) {
          if (form->empty()) continue;
          if (prefix.empty() || std::string_view(*form).starts_with(prefix)) {
            safePrint(*form);
            safePrint("\t");
            safePrintLn(opt.description);
          }
        }
      }
      if (opts.empty()) {
        auto items = getWindowsOptionCompletions(cmd, prefix);
        for (const auto &item : items) {
          safePrint(item.text);
          safePrint("\t");
          safePrintLn(item.hint);
        }
      }
      return 0;
    }

    // Check for top-level help flags/alias
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
      return printHelp();
    }

    // Check for version flags
    if (args.size() == 1 && (args[0] == "--version" || args[0] == "-v")) {
      safePrintLn(L"WinuxCmd " + utf8_to_wstring(WinuxCmd::VERSION_STRING));
      return 0;
    }

    if (!args.empty() && args[0] == "help") {
      if (args.size() == 1) return printHelp();
      if (args.size() == 2) {
        std::string topic(args[1]);
        if (CommandRegistry::hasCommand(topic)) {
          CommandRegistry::printHelp(topic);
          return 0;
        }
        std::string lowered = toLowerAscii(topic);
        if (CommandRegistry::hasCommand(lowered)) {
          CommandRegistry::printHelp(lowered);
          return 0;
        }
        safeErrorPrintLn("winuxcmd: no help topic for '" + topic + "'");
        return 1;
      }
      safeErrorPrintLn("winuxcmd: help accepts at most one command name");
      return 1;
    }

    // Extract command name and remaining arguments

    const std::string_view cmd_name = args[0];

    const std::span<std::string_view> cmd_args(args.data() + 1,

                                               args.size() - 1);

    // Check for --version in command arguments
    bool has_version = false;
    for (const auto &arg : cmd_args) {
      if (arg == "--version") {
        has_version = true;
        break;
      }
    }

    if (has_version && CommandRegistry::hasCommand(cmd_name)) {
      safePrintLn(L"WinuxCmd " + utf8_to_wstring(WinuxCmd::VERSION_STRING));
      return 0;
    }

    if (!CommandRegistry::hasCommand(cmd_name)) {
      std::string raw_line;

      raw_line.reserve(64);

      for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) raw_line.push_back(' ');

        raw_line.append(args[i]);
      }

      return runNativeFallback(raw_line);
    }

    // Direct execution
    // Dispatch the command to corresponding implementation
    return CommandRegistry::dispatch(cmd_name, cmd_args);
  } else {
    // Mode 2: <command>.exe [args...] (e.g., ls.exe -la)
    // Treat executable name as command name for direct calls
    const std::span<std::string_view> cmd_args(args.data(), args.size());

    // Dispatch the command to corresponding implementation
    return CommandRegistry::dispatch(self_name, cmd_args);
  }
}
