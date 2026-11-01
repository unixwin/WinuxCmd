// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for killall (psmisc semantics: exact
///     process-name matching by default, -r for regular expressions,
///     names containing a path separator match the executable image
///     path). On Windows every nonzero signal maps to the Win32
///     termination operation and signal 0 to an existence check.
/// @Version: 0.2.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

#include "commands/procps_select.h"

auto constexpr KILLALL_OPTIONS = std::array{
    OPTION("-e", "--exact", "require an exact process-name match"),
    OPTION("-I", "--ignore-case", "match case insensitively"),
    OPTION("-i", "--interactive",
           "ask for confirmation before killing each process"),
    OPTION("-l", "--list", "list all known signal names"),
    OPTION("-o", "--older-than",
           "match only processes older than the given time", STRING_TYPE),
    OPTION("-q", "--quiet", "do not complain if no processes were killed"),
    OPTION("-r", "--regexp",
           "interpret process names as extended regular expressions"),
    OPTION("-s", "--signal", "send the given signal instead of SIGTERM",
           STRING_TYPE),
    OPTION("-u", "--user", "kill only processes owned by the given user",
           STRING_TYPE),
    OPTION("-v", "--verbose", "report successful signals"),
    OPTION("-w", "--wait", "wait for all killed processes to die"),
    OPTION("-y", "--younger-than",
           "match only processes younger than the given time", STRING_TYPE),
    OPTION("-NUM", "", "", INT_TYPE),
    PROCPS_SIGNAL_SPELLING_OPTIONS};

namespace killall_pipeline {
namespace sel = procps_select;

struct Config {
  bool exact = false;
  bool ignore_case = false;
  bool interactive = false;
  bool list = false;
  bool quiet = false;
  bool regexp = false;
  bool verbose = false;
  bool wait = false;
  int signal = 15;  // SIGTERM
  std::optional<std::string> user_spec;
  std::optional<double> older_than;    // seconds of age
  std::optional<double> younger_than;  // seconds of age
  std::vector<std::wstring> names;
  std::vector<std::wregex> patterns;  // -r, parallel to names
};

struct ConfigError {
  std::string message;
};

// psmisc killall age values: a float followed by one unit suffix
// (s, m, h, d, w, M, y). Month/year use the average lengths.
auto parse_age(std::string_view text) -> std::optional<double> {
  if (text.empty()) return std::nullopt;
  double multiplier = 1.0;
  std::string_view number = text;
  switch (text.back()) {
    case 's':
      number.remove_suffix(1);
      break;
    case 'm':
      multiplier = 60.0;
      number.remove_suffix(1);
      break;
    case 'h':
      multiplier = 3600.0;
      number.remove_suffix(1);
      break;
    case 'd':
      multiplier = 86400.0;
      number.remove_suffix(1);
      break;
    case 'w':
      multiplier = 604800.0;
      number.remove_suffix(1);
      break;
    case 'M':
      multiplier = 2630016.0;  // 30.44 days
      number.remove_suffix(1);
      break;
    case 'y':
      multiplier = 31557600.0;  // 365.25 days
      number.remove_suffix(1);
      break;
    default:
      break;
  }
  if (number.empty()) return std::nullopt;
  const std::string str(number);
  char* end = nullptr;
  const double value = std::strtod(str.c_str(), &end);
  if (end == str.c_str() || *end != '\0') return std::nullopt;
  return value * multiplier;
}

auto build_config(const CommandContext<KILLALL_OPTIONS.size()>& ctx)
    -> std::expected<Config, ConfigError> {
  Config cfg;
  cfg.exact = ctx.get<bool>("-e", false) || ctx.get<bool>("--exact", false);
  cfg.ignore_case =
      ctx.get<bool>("-I", false) || ctx.get<bool>("--ignore-case", false);
  cfg.interactive =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--interactive", false);
  cfg.list = ctx.get<bool>("-l", false) || ctx.get<bool>("--list", false);
  cfg.quiet = ctx.get<bool>("-q", false) || ctx.get<bool>("--quiet", false);
  cfg.regexp = ctx.get<bool>("-r", false) || ctx.get<bool>("--regexp", false);
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  cfg.wait = ctx.get<bool>("-w", false) || ctx.get<bool>("--wait", false);

  // Signal: at most one -SIGNAL/-NUMBER token (a second is an error),
  // and -s/--signal overrides it, mirroring pkill.
  size_t spellings = 0;
  std::string spelling_name;
  for (auto name : sel::kSignalSpellingNames) {
    const size_t n = ctx.count({name});
    if (n > 0) {
      spellings += n;
      spelling_name = std::string(name.substr(1));
    }
  }
  const bool has_num = ctx.has("-NUM");
  if (has_num) spellings += ctx.count({"-NUM"});
  if (spellings > 1) {
    return std::unexpected(
        ConfigError{"killall: only one signal may be specified"});
  }
  if (ctx.has("-s") || ctx.has("--signal")) {
    const auto text = ctx.get<std::string>("--signal", "");
    const int number = sel::signal_name_to_number(text);
    if (number < 0) {
      return std::unexpected(
          ConfigError{"killall: invalid signal: '" + text + "'"});
    }
    cfg.signal = number;
  } else if (has_num) {
    const int number = ctx.get<int>("-NUM", 15);
    if (number < 0 || number > 64) {
      return std::unexpected(ConfigError{"killall: invalid signal: '" +
                                         std::to_string(number) + "'"});
    }
    cfg.signal = number;
  } else if (!spelling_name.empty()) {
    const int number = sel::signal_name_to_number(spelling_name);
    if (number < 0) {
      return std::unexpected(
          ConfigError{"killall: invalid signal: '" + spelling_name + "'"});
    }
    cfg.signal = number;
  }

  if (ctx.has("-u") || ctx.has("--user")) {
    auto spec = ctx.get<std::string>("--user", "");
    if (!sel::all_digits(spec) &&
        !win32_lookup_account(utf8_to_wstring(spec))) {
      return std::unexpected(
          ConfigError{"killall: invalid user: '" + spec + "'"});
    }
    cfg.user_spec = std::move(spec);
  }
  if (ctx.has("-o") || ctx.has("--older-than")) {
    const auto text = ctx.get<std::string>("--older-than", "");
    cfg.older_than = parse_age(text);
    if (!cfg.older_than) {
      return std::unexpected(
          ConfigError{"killall: invalid time specification: '" + text + "'"});
    }
  }
  if (ctx.has("-y") || ctx.has("--younger-than")) {
    const auto text = ctx.get<std::string>("--younger-than", "");
    cfg.younger_than = parse_age(text);
    if (!cfg.younger_than) {
      return std::unexpected(
          ConfigError{"killall: invalid time specification: '" + text + "'"});
    }
  }

  for (auto arg : ctx.positionals) {
    cfg.names.push_back(utf8_to_wstring(std::string(arg)));
  }
  if (cfg.regexp) {
    auto flags = std::regex_constants::ECMAScript;
    if (cfg.ignore_case) flags |= std::regex_constants::icase;
    for (const auto& name : cfg.names) {
      try {
        cfg.patterns.emplace_back(name, flags);
      } catch (const std::regex_error&) {
        return std::unexpected(
            ConfigError{"killall: invalid regular expression: '" +
                        wstring_to_utf8(name) + "'"});
      }
    }
  }
  return cfg;
}

auto fold_case(std::wstring text, bool ignore_case) -> std::wstring {
  return ignore_case ? ascii_lower_copy(std::move(text)) : std::move(text);
}

auto normalize_path(std::wstring path) -> std::wstring {
  std::ranges::replace(path, L'/', L'\\');
  return ascii_lower_copy(std::move(path));
}

// One name against one process. Names containing a path separator
// select by executable image path (psmisc killall); -r names are
// regular expressions over the process base name; anything else is an
// exact base-name match (psmisc default, with or without -e: Windows
// process names are never truncated to the 15-char comm limit, so the
// truncation rule behind -e has no second behavior here).
auto name_matches(const Win32ProcessInfo& proc, size_t name_index,
                  const Config& cfg) -> bool {
  const std::wstring& name = cfg.names[name_index];
  if (name.find_first_of(L"/\\") != std::wstring::npos) {
    const auto image = win32_process_image_path(proc.pid);
    if (image.empty()) return false;
    return normalize_path(image) == normalize_path(name);
  }
  std::wstring base = win32_basename_without_exe(proc.name);
  if (cfg.regexp) {
    return std::regex_search(base, cfg.patterns[name_index]);
  }
  return fold_case(std::move(base), cfg.ignore_case) ==
         fold_case(win32_basename_without_exe(name), cfg.ignore_case);
}

auto filters_match(const Win32ProcessInfo& proc, const Config& cfg,
                   unsigned long long now_ft) -> bool {
  if (cfg.user_spec) {
    auto owner = win32_process_account(proc.pid);
    if (!owner ||
        !win32_account_matches(*cfg.user_spec, owner->name, owner->id)) {
      return false;
    }
  }
  if (cfg.older_than || cfg.younger_than) {
    auto created = win32_process_creation_filetime(proc.pid);
    if (!created) return false;
    const double age = now_ft > *created
                           ? static_cast<double>(now_ft - *created) / 1.0e7
                           : 0.0;
    if (cfg.older_than && !(age > *cfg.older_than)) return false;
    if (cfg.younger_than && !(age < *cfg.younger_than)) return false;
  }
  return true;
}

auto confirm_kill(const Win32ProcessInfo& proc) -> bool {
  safeErrorPrint("Kill " + wstring_to_utf8(proc.name) + "(" +
                 std::to_string(proc.pid) + ")? (y/N) ");
  std::string answer;
  if (!std::getline(std::cin, answer)) return false;
  return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
}

auto print_signal_list() -> void {
  // psmisc killall -l lists the known signal names; the layout here
  // follows this product's kill -l convention (space-separated names,
  // wrapped near column 72) over the Linux signal table.
  std::string out;
  int column = 0;
  auto emit = [&](const std::string& name) {
    if (column > 0) {
      out += ' ';
      ++column;
    }
    out += name;
    column += static_cast<int>(name.size());
    if (column >= 72) {
      out += '\n';
      column = 0;
    }
  };
  for (const auto& entry : sel::kLinuxSignals) emit(entry.name);
  emit("RTMIN");
  emit("RTMAX");
  if (column > 0) out += '\n';
  safePrint(out);
}

auto run(const Config& cfg) -> int {
  auto processes = enumerate_win32_processes(false);
  const DWORD self = GetCurrentProcessId();
  const unsigned long long now_ft = sel::now_filetime();
  const bool by_user_only = cfg.names.empty();

  std::vector<DWORD> signaled_pids;
  bool all_satisfied = true;
  bool any_signaled = false;

  // Each listed name must produce at least one signaled process; with
  // no names, the -u criterion alone decides (psmisc exit contract).
  const size_t rounds = by_user_only ? 1 : cfg.names.size();
  for (size_t round = 0; round < rounds; ++round) {
    bool found = false;
    bool signaled = false;
    for (const auto& proc : processes) {
      if (proc.pid == 0 || proc.pid == 4 || proc.pid == self) continue;
      if (!by_user_only && !name_matches(proc, round, cfg)) continue;
      if (!filters_match(proc, cfg, now_ft)) continue;
      found = true;
      if (cfg.interactive && !confirm_kill(proc)) continue;
      auto result = cfg.signal == 0 ? win32_check_process(proc.pid)
                                    : win32_terminate_process(proc.pid);
      if (result.ok) {
        signaled = true;
        any_signaled = true;
        signaled_pids.push_back(proc.pid);
        if (cfg.verbose && cfg.signal != 0) {
          safePrintLn(std::to_wstring(proc.pid) + L" " + proc.name);
        }
        continue;
      }
      if (result.error == ERROR_INVALID_PARAMETER) {
        // Already exited: the goal state is reached.
        signaled = true;
        any_signaled = true;
        continue;
      }
      if (!cfg.quiet) {
        safeErrorPrintLn("killall: " + std::to_string(proc.pid) + ": " +
                         result.message);
      }
    }
    if (!found) {
      if (!by_user_only && !cfg.quiet) {
        safeErrorPrintLn(wstring_to_utf8(cfg.names[round]) +
                         ": no process found");
      }
      all_satisfied = false;
    } else if (!signaled) {
      all_satisfied = false;
    }
  }

  if (cfg.wait && !signaled_pids.empty()) {
    // psmisc polls once per second until every signaled process is
    // gone (and warns it may wait forever for ignored signals).
    while (true) {
      bool any_alive = false;
      for (DWORD pid : signaled_pids) {
        if (win32_check_process(pid).ok) {
          any_alive = true;
          break;
        }
      }
      if (!any_alive) break;
      Sleep(1000);
    }
  }
  return all_satisfied && any_signaled ? 0 : 1;
}

}  // namespace killall_pipeline

REGISTER_COMMAND(killall, "killall", "killall [OPTION]... NAME...",
                 "Signal processes by executable name.\n"
                 "On Windows, TERM/KILL are mapped to Win32 termination.",
                 "  killall notepad\n"
                 "  killall -0 explorer\n"
                 "  killall -u alice -r 'worker.*'",
                 "kill(1), pkill(1), pgrep(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", KILLALL_OPTIONS) {
  using namespace killall_pipeline;

  auto built = build_config(ctx);
  if (!built) {
    safeErrorPrintLn(built.error().message);
    safeErrorPrintLn("Try 'killall --help' for more information.");
    return 1;
  }
  const Config& cfg = *built;
  if (cfg.list) {
    print_signal_list();
    return 0;
  }
  if (cfg.names.empty() && !cfg.user_spec) {
    safeErrorPrintLn("killall: missing process name");
    safeErrorPrintLn("Try 'killall --help' for more information.");
    return 1;
  }
  return run(cfg);
}
