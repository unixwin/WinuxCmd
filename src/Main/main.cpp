// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
// src/main.cpp
// Main entry point for WinuxCmd
#include <windows.h>

#include "../shared/argv_adapters.h"

import std;
import core;
import utils;
import version;

namespace {
static void applyInheritedStdbuf(const char* name, FILE* stream) {
  const char* mode = std::getenv(name);
  if (mode == nullptr || *mode == '\0') return;
  if (std::strcmp(mode, "0") == 0) {
    setvbuf(stream, nullptr, _IONBF, 0);
    return;
  }
  if (std::strcmp(mode, "L") == 0) {
    // MSVC's CRT does not support POSIX line buffering reliably. Accept the
    // inherited GNU-compatible marker without mutating the stream.
    return;
  }
  char* end = nullptr;
  const auto size = std::strtoull(mode, &end, 10);
  if (end != mode && *end == '\0' && size > 0 &&
      size <= std::numeric_limits<size_t>::max()) {
    setvbuf(stream, nullptr, _IOFBF, static_cast<size_t>(size));
  }
}

static std::string toLowerAscii(std::string s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static void printCommandSummary(std::string_view name, std::string_view desc,
                                std::string_view command_style, bool color) {
  constexpr size_t command_width = 12;
  std::string command(name);
  if (command.size() < command_width) {
    command.append(command_width - command.size(), ' ');
  }

  const std::string first_prefix =
      "  " + (color ? colorizeStdout(command, command_style) : command) + " ";
  const std::string continuation_prefix(2 + command_width + 1, ' ');

  const auto translated_desc = winux::i18n::translate(
      "command." + std::string(name) + ".synopsis", desc);
  desc = translated_desc;
  bool first_line = true;
  while (true) {
    size_t newline_pos = desc.find('\n');
    std::string_view line = newline_pos == std::string_view::npos
                                ? desc
                                : desc.substr(0, newline_pos);
    safePrintLn((first_line ? first_prefix : continuation_prefix) +
                std::string(line));
    first_line = false;

    if (newline_pos == std::string_view::npos) break;
    desc = desc.substr(newline_pos + 1);
  }
}

// The argv-adapter table (opt/ payloads that need a fixed argv prefix,
// e.g. gawk's `-v BINMODE=3`; unixwin/niubash#467) lives in
// src/shared/argv_adapters.h, shared with the package shim's forward
// path - see that header for the full rationale.

/// Skip `count` leading whitespace-separated tokens of a Windows command
/// line, honouring the double-quoted token form. Used to drop the program
/// token - and, when the command came in through the `winuxcmd <verb>` form,
/// the verb as well - before the rest is forwarded verbatim.
static std::wstring_view skip_command_line_tokens(std::wstring_view raw,
                                                  size_t count) noexcept {
  while (count-- > 0) {
    const size_t begin = raw.find_first_not_of(L" \t");
    if (begin == std::wstring_view::npos) return {};
    raw.remove_prefix(begin);
    if (raw[0] == L'"') {
      const size_t closing = raw.find(L'"', 1);
      if (closing == std::wstring_view::npos) return {};
      raw.remove_prefix(closing + 1);
    } else {
      const size_t space = raw.find_first_of(L" \t");
      if (space == std::wstring_view::npos) return {};
      raw.remove_prefix(space);
    }
  }
  const size_t start = raw.find_first_not_of(L" \t");
  return start == std::wstring_view::npos ? std::wstring_view{} : raw.substr(start);
}

// Shim-layout packages (installed via `wpm install` with artifact
// layout=shim) keep their payload self-contained under <root>\opt\<pkg>\.
// The command entry in usr\bin is a plain hardlink of winuxcmd.exe; when the
// invoked name is not a registered command, we forward to
// <root>\opt\<name>\<name>.exe so the payload's private DLLs resolve from its
// own directory. Returns nullopt when no payload exists (caller falls back to
// the regular command-not-found path). `via_verb` marks the
// `winuxcmd <name>` form, where the command line still carries the verb token
// ahead of the real arguments.
static std::optional<int> forwardToOptPayload(std::string_view name,
                                              bool via_verb) noexcept {
  namespace fs = std::filesystem;
  wchar_t buffer[MAX_PATH];
  const DWORD size = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (size == 0 || size >= MAX_PATH) return std::nullopt;
  const fs::path self(buffer);
  // Canonical placement is <root>\usr\bin\<self>.exe, three directories below
  // the root. Commands forwarded through an argv adapter may also sit at
  // <root>\bin\<self>.exe (the PATH-entry link wpm points at a forwarder for
  // them, unixwin/niubash#467), one level closer - probe the canonical depth
  // first, then the shallower one, for the opt\ tree.
  const fs::path root_canonical = self.parent_path().parent_path().parent_path();
  std::error_code root_ec;
  const fs::path root =
      fs::is_directory(root_canonical / L"opt", root_ec)
          ? root_canonical
          : self.parent_path().parent_path();

  const fs::path payload_direct =
      root / L"opt" / utf8_to_wstring(std::string(name)) /
      (utf8_to_wstring(std::string(name)) + L".exe");
  std::error_code ec;
  fs::path payload = payload_direct;
  if (!fs::is_regular_file(payload, ec)) {
    // Package dir may differ from the command name (e.g.
    // opt\sysinternals-suite\ provides accesschk.exe), and toolchain payloads
    // may nest their exes (e.g. opt\go\bin\go.exe): resolve the payload from
    // each package's shallow, well-known locations first (breadth-first,
    // shallowest match wins), and only fall back to a bounded recursive scan
    // for exotic nested layouts.
    //
    // PERF (issue niu-git#8): the previous whole-tree recursive_directory_iterator
    // over <root>\opt\ walked every file looking for a shallow <name>.exe.
    // When the command name differs from the package name (git -> opt\niugit\)
    // that scan traversed ~15k files on every invocation (~170ms) before a
    // match, which dominated `git` startup (220ms vs 56ms direct). Checking
    // the few well-known shallow locations makes this O(#packages) and
    // restores near-native forwarding speed for every shim package.
    const fs::path opt_dir = root / L"opt";
    if (!fs::exists(opt_dir, ec)) return std::nullopt;
    const std::wstring wanted = utf8_to_wstring(std::string(name)) + L".exe";
    bool found = false;
    size_t best_depth = SIZE_MAX;

    // Shallow payload locations per package, ascending by depth. Depth is
    // relative to <root>\opt\ (same scale the old iterator used): a package
    // root exe is depth 1, <pkg>\bin\<exe> depth 2, etc.
    struct PayloadLocation {
      std::wstring_view subdir;
      size_t depth;
    };
    static constexpr PayloadLocation kPayloadLocations[] = {
        {L"", 1}, {L"bin", 2}, {L"libexec", 2}, {L"lib", 2},
        {L"sbin", 2}, {L"usr\\bin", 3}};
    for (fs::directory_iterator pkg_it(opt_dir, ec);
         pkg_it != fs::directory_iterator(); pkg_it.increment(ec)) {
      if (ec) break;
      if (!pkg_it->is_directory(ec)) continue;
      const fs::path pkg = pkg_it->path();
      for (const auto& loc : kPayloadLocations) {
        fs::path cand = pkg;
        if (!loc.subdir.empty()) cand /= loc.subdir;
        cand /= wanted;
        std::error_code file_ec;
        if (fs::is_regular_file(cand, file_ec) && !file_ec) {
          if (loc.depth < best_depth) {
            payload = cand;
            best_depth = loc.depth;
            found = true;
          }
        }
      }
      // A depth-1 (package root) match is globally shallowest: stop early
      // rather than walking the remaining packages.
      if (found && best_depth == 1) break;
    }

    if (!found) {
      // Fallback for exotic nested layouts: bounded recursive scan,
      // shallowest match wins, up to the small depth bound.
      auto iequals_wanted = [&wanted](const std::wstring& candidate) {
        if (candidate.size() != wanted.size()) return false;
        for (size_t i = 0; i < candidate.size(); ++i) {
          wchar_t a = candidate[i], b = wanted[i];
          if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
          if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
          if (a != b) return false;
        }
        return true;
      };
      fs::recursive_directory_iterator it(
          opt_dir, fs::directory_options::skip_permission_denied, ec);
      for (; it != fs::end(it); it.increment(ec)) {
        if (ec) break;
        const size_t depth = static_cast<size_t>(it.depth());
        if (depth > 3 || depth >= best_depth) continue;
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec)) continue;
        if (!iequals_wanted(it->path().filename().wstring())) continue;
        payload = it->path();
        found = true;
        best_depth = depth;
        if (best_depth == 0) break;
      }
      if (!found) return std::nullopt;
    }
  }

  // Rebuild the child command line from the raw one so the original quoting
  // of every argument is preserved verbatim. `winuxcmd <name> args` carries
  // one extra token (the verb) the payload must never see.
  const std::wstring raw = GetCommandLineW();
  const std::wstring rest(skip_command_line_tokens(raw, via_verb ? 2 : 1));
  const wchar_t* adapter_prefix =
      argv_adapters::prefix_for(name.data(), name.size());
  std::wstring command_line = L"\"" + payload.wstring() + L"\"" +
                              (adapter_prefix ? adapter_prefix : L"");
  if (!rest.empty()) command_line += L" " + rest;

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(payload.c_str(), command_line.data(), nullptr, nullptr,
                      TRUE, 0, nullptr, nullptr, &si, &pi)) {
    safeErrorPrintLn(winux::i18n::format(
        "main.error.shim_launch_failed",
        "winuxcmd: failed to launch shim payload '{}': error {}",
        std::string(name), static_cast<unsigned long>(GetLastError())));
    return 126;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return static_cast<int>(exit_code);
}

// GNU coreutils-style presentation groups for the bare `winuxcmd` listing
// (#1155). Sections render alphabetically inside; a registered command that
// is missing from every table still shows up under "Other utilities", so
// adding a command never requires touching this table to stay visible.
constexpr std::string_view kFileOperationsCommands[] = {
    "basename", "chattr",  "chcon", "chgrp", "chmod",   "chown",     "cp",
    "cpio",     "dd",      "df",    "dir",   "dirname", "dircolors", "du",
    "file",     "install", "ln",    "ls",    "lsattr",  "mkdir",     "mkfifo",
    "mknod",    "mktemp",  "mv",    "namei", "pathchk", "readlink",  "realpath",
    "rm",       "rmdir",   "shred", "stat",  "sync",    "touch",     "tree",
    "truncate", "unlink",  "vdir"};

constexpr std::string_view kTextProcessingCommands[] = {
    "cat",      "col",    "column",   "comm",     "csplit", "cut",  "d2u",
    "dos2unix", "expand", "fmt",      "fold",     "head",   "join", "look",
    "nl",       "paste",  "pr",       "ptx",      "rev",    "sed",  "shuf",
    "sort",     "split",  "tac",      "tail",     "tee",    "tr",   "tsort",
    "u2d",      "uniq",   "unix2dos", "unexpand", "wc"};

constexpr std::string_view kComparePatchCommands[] = {"cmp", "diff", "diff3",
                                                      "patch", "sdiff"};

constexpr std::string_view kNumericOperationsCommands[] = {"factor", "mpicalc",
                                                           "numfmt", "seq"};

constexpr std::string_view kSearchToolsCommands[] = {
    "egrep",    "fgrep",   "find",  "grep", "locate",
    "updatedb", "whereis", "which", "xargs"};

constexpr std::string_view kChecksumEncodingCommands[] = {
    "b2sum",     "base32",    "base64",  "basenc",  "cksum",     "hexdump",
    "hmac256",   "md5sum",    "od",      "sha1sum", "sha224sum", "sha256sum",
    "sha384sum", "sha512sum", "strings", "sum",     "xxd"};

constexpr std::string_view kShellUtilitiesCommands[] = {
    "[",      "clear", "echo",    "env",  "envsubst", "expr",  "false",
    "getopt", "nohup", "printf",  "pwd",  "printenv", "sleep", "stdbuf",
    "test",   "time",  "timeout", "true", "yes"};

constexpr std::string_view kSystemProcessCommands[] = {
    "arch",    "cal",     "chroot",   "cygpath", "date",  "free",    "getconf",
    "getfacl", "groups",  "hostname", "hostid",  "id",    "kill",    "killall",
    "ldd",     "locale",  "logger",   "logname", "lsof",  "mkgroup", "mkpasswd",
    "nice",    "nproc",   "pgrep",    "pidof",   "pinky", "pkill",   "pldd",
    "ps",      "regtool", "renice",   "runcon",  "top",   "tzset",   "uname",
    "uptime",  "users",   "vmstat",   "w",       "watch", "who",     "whoami",
    "wpm"};

constexpr std::string_view kTerminalDocumentationCommands[] = {
    "infocmp", "less", "man", "more", "reset",
    "stty",    "tic",  "toe", "tput", "tty"};

struct CommandCategory {
  std::string_view title_key;
  std::string_view title_fallback;
  std::span<const std::string_view> commands;
};

constexpr CommandCategory kCommandCategories[] = {
    {"main.category.file_operations",
     "File operations:", kFileOperationsCommands},
    {"main.category.text_processing",
     "Text processing:", kTextProcessingCommands},
    {"main.category.compare_patch", "Compare & patch:", kComparePatchCommands},
    {"main.category.numeric_operations",
     "Numeric operations:", kNumericOperationsCommands},
    {"main.category.search_tools", "Search tools:", kSearchToolsCommands},
    {"main.category.checksum_encoding",
     "Checksums & encoding:", kChecksumEncodingCommands},
    {"main.category.shell_utilities",
     "Shell utilities:", kShellUtilitiesCommands},
    {"main.category.system_process",
     "System & process:", kSystemProcessCommands},
    {"main.category.terminal_docs",
     "Terminal & documentation:", kTerminalDocumentationCommands},
};

void printGuideLine(std::string_view key, std::string_view fallback) {
  safePrintLn(winux::i18n::translate(key, fallback));
}

void printSectionTitle(std::string_view title, const std::string& style,
                       bool color) {
  safePrintLn(color ? colorizeStdout(std::string(title), style)
                    : std::string(title));
}

void printCommandGroup(
    const std::vector<std::pair<std::string_view, std::string_view>>& members,
    const std::string& command_style, bool color) {
  for (const auto& [cmd_name, cmd_desc] : members) {
    printCommandSummary(cmd_name, cmd_desc, command_style, color);
  }
}

}  // namespace

/**
 * @brief Print help information
 * @return Exit code (1 - error)
 */
static int printHelp() noexcept {
  const bool color = shouldUseAnsiColorStdout();
  const std::string title_style =
      std::string(ANSI_BOLD) + ansiFgRgb(98, 214, 255);
  const std::string section_style =
      std::string(ANSI_BOLD) + ANSI_UNDERLINE + ansiFg256(82);
  const std::string command_style = std::string(ANSI_BOLD) + ansiFg256(117);
  const std::string subtle_style = ansiFg256(245);

  const auto subtitle = winux::i18n::translate(
      "main.subtitle", "Windows Compatible Linux Command Set");
  const auto usage = winux::i18n::translate("common.usage", "Usage:");
  safePrintLn(color ? colorizeStdout("WinuxCmd", title_style) + " - " + subtitle
                    : "WinuxCmd - " + subtitle);
  safePrintLn(color ? colorizeStdout(usage, section_style) +
                          " winuxcmd <command> [options]..."
                    : usage + " winuxcmd <command> [options]...");
  safePrintLn("");

  // Getting-started guide (#1155): three discovery paths plus the flat
  // listing, printed before the groups so first contact teaches navigation.
  const auto guide_title =
      winux::i18n::translate("main.guide.title", "Help and discovery:");
  printSectionTitle(guide_title, section_style, color);
  printGuideLine("main.guide.help_flag",
                 "  winuxcmd <command> --help     show usage for a command");
  printGuideLine(
      "main.guide.help_command",
      "  winuxcmd help <command>       show the full help for a command");
  printGuideLine(
      "main.guide.list_all",
      "  winuxcmd --list-all           list every command, ungrouped");
  printGuideLine(
      "main.guide.search",
      "  wpm search <capability>       find and install extra packages");
  safePrintLn("");

  // Grouped listing: registered commands bucketed into the categories above,
  // alphabetical inside each section. Commands missing from every bucket are
  // collected into "Other utilities" so a new command is never invisible.
  auto commands = CommandRegistry::getAllCommands();
  std::ranges::sort(commands);

  std::map<std::string_view, std::string_view> by_name;
  for (const auto& [cmd_name, cmd_desc] : commands) {
    by_name.emplace(cmd_name, cmd_desc);
  }

  std::set<std::string_view> categorized;
  for (const auto& category : kCommandCategories) {
    std::vector<std::pair<std::string_view, std::string_view>> members;
    for (const auto& name : category.commands) {
      if (auto it = by_name.find(name); it != by_name.end()) {
        members.emplace_back(*it);
        categorized.insert(name);
      }
    }
    if (members.empty()) continue;
    std::ranges::sort(members);
    const auto title =
        winux::i18n::translate(category.title_key, category.title_fallback);
    printSectionTitle(title, section_style, color);
    printCommandGroup(members, command_style, color);
    safePrintLn("");
  }

  std::vector<std::pair<std::string_view, std::string_view>> others;
  for (const auto& [cmd_name, cmd_desc] : commands) {
    if (!categorized.contains(cmd_name)) {
      others.emplace_back(cmd_name, cmd_desc);
    }
  }
  if (!others.empty()) {
    std::ranges::sort(others);
    const auto title =
        winux::i18n::translate("main.category.other", "Other utilities:");
    printSectionTitle(title, section_style, color);
    printCommandGroup(others, command_style, color);
    safePrintLn("");
  }

  const auto tip = winux::i18n::translate(
      "main.help_tip",
      "Tip: Use 'winuxcmd <command> --help' for command-specific help.");
  safePrintLn(color ? colorizeStdout("Tip:", subtle_style) + " " + tip : tip);

  // i18n discoverability (#1155): when the system language is not English
  // and its WPM catalog is not installed, point at the install command.
  if (const auto hint = winux::i18n::locale_hint(); !hint.empty()) {
    safePrintLn(color ? colorizeStdout(hint, subtle_style) : hint);
  }

  // GNU-style tail: the portal link is the stable last line of every help
  // variant (bare `winuxcmd`, `--help`, `help` alias).
  const auto home =
      winux::i18n::translate("common.home", "Home: <https://unixwin.dev>");
  safePrintLn(color ? colorizeStdout(home, subtle_style) : home);
  return 1;
}

/**
 * @brief Print the flat, alphabetical list of every registered command
 *        (`winuxcmd --list-all`, #1155)
 * @return Exit code (0 - success)
 */
static int printCommandList() noexcept {
  const bool color = shouldUseAnsiColorStdout();
  const std::string command_style = std::string(ANSI_BOLD) + ansiFg256(117);

  auto commands = CommandRegistry::getAllCommands();
  std::ranges::sort(commands);
  printCommandGroup(commands, command_style, color);
  return 0;
}

/**
 * @brief Main function for WinuxCmd
 * @param argc Number of command-line arguments
 * @param wargv Array of command-line arguments (UTF-16)
 * @return Exit code from the executed command (0 = success, non-zero = error)
 *
 * Wide entry point (niubash #88): the narrow CRT main hands out argv decoded
 * with the system ACP (e.g. GBK on zh-CN installs), which downstream code
 * misreads as UTF-8 and turns non-ASCII arguments into U+FEBF-style mojibake
 * even when the caller passed a perfect UTF-16 command line. Decoding the
 * wide argv with wstring_to_utf8 at this single boundary makes argv valid
 * UTF-8 regardless of the process code page.
 */
int wmain(int argc, wchar_t* wargv[]) noexcept {
  if (argc < 1) {
    return printHelp();
  }
  // Automatically set console or pipe output.
  setupConsoleForUnicode();
  applyInheritedStdbuf("WINUX_STDBUF_I", stdin);
  applyInheritedStdbuf("WINUX_STDBUF_O", stdout);
  applyInheritedStdbuf("WINUX_STDBUF_E", stderr);

  // Own the UTF-8 copies for the whole run; args below only borrow views.
  std::vector<std::string> owned_args;
  owned_args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    owned_args.emplace_back(wstring_to_utf8(wargv[i]));
  }

  // Get the executable name (stem only)
  std::string self_name = path::get_executable_name(owned_args[0].c_str());

  // Convert command-line arguments to string_views for efficiency
  std::vector<std::string_view> args;
  args.reserve(owned_args.size() - 1);
  for (size_t i = 1; i < owned_args.size(); ++i) {
    args.emplace_back(owned_args[i]);
  }

  if (self_name == "winuxcmd") {
    // Mode 1: winuxcmd <command> [args...] (e.g., winuxcmd ls -la)
    if (args.empty()) {
      return printHelp();
    }

    // #1155: explicit flat listing of every registered command.
    if (args.size() == 1 && args[0] == "--list-all") {
      return printCommandList();
    }

    // Check for top-level help flag/alias. Like GNU getopt_long, accept any
    // unambiguous abbreviation (e.g. --hel, --he for --help).
    if (args.size() == 1 && args[0].size() >= 3 &&
        std::string_view("--help").starts_with(args[0])) {
      return printHelp();
    }

    // Check for version flags (unambiguous --version abbreviations too)
    if (args.size() == 1 &&
        (args[0] == "-v" ||
         (args[0].size() >= 3 &&
          std::string_view("--version").starts_with(args[0])))) {
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
        safeErrorPrintLn(winux::i18n::format("main.error.no_help_topic",
                                             "winuxcmd: no help topic for '{}'",
                                             topic));
        return 1;
      }
      safeErrorPrintLn(winux::i18n::translate(
          "main.error.help_too_many_topics",
          "winuxcmd: help accepts at most one command name"));
      return 1;
    }

    // Extract command name and remaining arguments

    const std::string_view cmd_name = args[0];

    const std::span<std::string_view> cmd_args(args.data() + 1,

                                               args.size() - 1);

    // Check for --version in command arguments.  [GNU] Commands whose
    // arguments are data (echo, yes, test, [, true, false, printf, expr) do
    // not honor "--version" in any position — e.g. `printf '%s\n' --version`
    // prints "--version" and `expr 1 --version` is a syntax error — so the
    // dispatcher's per-command rule applies to them instead of this scan.
    // The set itself is core's is_literal_arg_command, the single source of
    // truth shared with the dispatcher's abbreviation expansion: WinuxCmd
    // #1215 was this scan's hand-copied table missing "expr", so
    // `winuxcmd expr 1 --version` printed the top-level "WinuxCmd <ver>"
    // and exited 0, while the expr.exe shim (and GNU) reject the operand
    // with exit 2.
    const bool literal_arg_command = is_literal_arg_command(cmd_name);
    bool has_version = false;
    if (!literal_arg_command) {
      for (const auto& arg : cmd_args) {
        if (arg == "--version") {
          has_version = true;
          break;
        }
      }
    }

    if (has_version && CommandRegistry::hasCommand(cmd_name)) {
      safePrintLn(L"WinuxCmd " + utf8_to_wstring(WinuxCmd::VERSION_STRING));
      return 0;
    }

    if (!CommandRegistry::hasCommand(cmd_name)) {
      // Not a builtin: try forwarding to a shim-layout payload first.
      if (auto forwarded = forwardToOptPayload(cmd_name, /*via_verb=*/true)) {
        return *forwarded;
      }
      safeErrorPrintLn(winux::i18n::format("core.error.command_not_found",
                                           "winuxcmd: command not found: {}",
                                           cmd_name));
      return 127;
    }

    // Direct execution
    // Dispatch the command to corresponding implementation
    return CommandRegistry::dispatch(cmd_name, cmd_args);
  } else {
    // Mode 2: <command>.exe [args...] (e.g., ls.exe -la)
    // Treat executable name as command name for direct calls
    const std::span<std::string_view> cmd_args(args.data(), args.size());

    // Not a builtin: try forwarding to a shim-layout payload first.
    if (!CommandRegistry::hasCommand(self_name)) {
      if (auto forwarded = forwardToOptPayload(self_name, /*via_verb=*/false)) {
        return *forwarded;
      }
    }

    // Dispatch the command to corresponding implementation
    return CommandRegistry::dispatch(self_name, cmd_args);
  }
}
