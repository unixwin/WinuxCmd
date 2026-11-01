// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pkill (procps 4.0.4 semantics; the
///     selection engine lives in procps_select.h). On Windows every
///     nonzero signal maps to the Win32 termination operation and
///     signal 0 maps to an existence check, matching kill(2) probing.
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

auto constexpr PKILL_OPTIONS = std::array{
    OPTION("-A", "--ignore-ancestors",
           "ignore the ancestors of the pkill process"),
    OPTION("-c", "--count", "print the count of matching processes"),
    OPTION("-e", "--echo", "display what is killed"),
    OPTION("-f", "--full", "match against full command line"),
    OPTION("-F", "--pidfile", "read PID from file", STRING_TYPE),
    OPTION("-G", "--group", "match by real group ID or name", STRING_TYPE),
    OPTION("-i", "--ignore-case", "match case insensitively"),
    OPTION("-L", "--logpidfile", "fail if the pidfile is not locked"),
    OPTION("-n", "--newest", "select the most recently started process"),
    OPTION("-o", "--oldest", "select the oldest process"),
    OPTION("-O", "--older", "select processes older than the given seconds",
           STRING_TYPE),
    OPTION("-P", "--parent", "match by parent PID", STRING_TYPE),
    OPTION("-q", "--queue", "use sigqueue(2) rather than kill(2)", INT_TYPE),
    OPTION("-s", "--session", "match by session ID (0 means own session)",
           STRING_TYPE),
    OPTION("-u", "--euid", "match by effective user ID or name", STRING_TYPE),
    OPTION("-U", "--uid", "match by real user ID or name", STRING_TYPE),
    OPTION("-x", "--exact", "match the whole name or command line"),
    OPTION("", "--signal", "send the given signal instead of SIGTERM",
           STRING_TYPE),
    OPTION("-NUM", "", "", INT_TYPE),
    PROCPS_SIGNAL_SPELLING_OPTIONS};

namespace pkill_pipeline {
namespace sel = procps_select;

struct Config {
  sel::Criteria criteria;
  bool count = false;
  bool echo = false;
  int signal = 15;  // SIGTERM
  bool has_pidfile = false;
  std::string pidfile_path;
  bool logpidfile = false;
};

struct ConfigError {
  std::string message;
  int exit_code = 2;
};

// Resolve the signal specification the way pgrep.c parse_opts() does
// for pkill: at most one -SIGNAL/-NUMBER token may appear (a second
// one is a usage error upstream), and --signal overrides it.
auto resolve_signal(const CommandContext<PKILL_OPTIONS.size()>& ctx)
    -> std::expected<int, ConfigError> {
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
        ConfigError{"pkill: only one signal may be specified"});
  }
  if (ctx.has("--signal")) {
    const auto text = ctx.get<std::string>("--signal", "");
    const int number = sel::signal_name_to_number(text);
    if (number < 0) {
      return std::unexpected(
          ConfigError{"pkill: invalid signal: '" + text + "'"});
    }
    return number;
  }
  if (has_num) {
    const int number = ctx.get<int>("-NUM", 15);
    if (number < 0 || number > 64) {
      return std::unexpected(ConfigError{"pkill: invalid signal: '" +
                                         std::to_string(number) + "'"});
    }
    return number;
  }
  if (!spelling_name.empty()) {
    const int number = sel::signal_name_to_number(spelling_name);
    if (number < 0) {
      return std::unexpected(
          ConfigError{"pkill: invalid signal: '" + spelling_name + "'"});
    }
    return number;
  }
  return 15;
}

auto build_config(const CommandContext<PKILL_OPTIONS.size()>& ctx)
    -> std::expected<Config, ConfigError> {
  Config cfg;
  auto& crit = cfg.criteria;
  crit.full = ctx.get<bool>("-f", false) || ctx.get<bool>("--full", false);
  const bool ignore_case =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--ignore-case", false);
  const bool exact =
      ctx.get<bool>("-x", false) || ctx.get<bool>("--exact", false);
  cfg.count = ctx.get<bool>("-c", false) || ctx.get<bool>("--count", false);
  cfg.echo = ctx.get<bool>("-e", false) || ctx.get<bool>("--echo", false);
  cfg.logpidfile =
      ctx.get<bool>("-L", false) || ctx.get<bool>("--logpidfile", false);
  crit.ignore_ancestors =
      ctx.get<bool>("-A", false) || ctx.get<bool>("--ignore-ancestors", false);
  crit.newest = ctx.get<bool>("-n", false) || ctx.get<bool>("--newest", false);
  crit.oldest = ctx.get<bool>("-o", false) || ctx.get<bool>("--oldest", false);
  // pgrep.c: -n and -o reject any combination, including repeats.
  if (ctx.count({"-n", "--newest", "-o", "--oldest"}) > 1) {
    return std::unexpected(
        ConfigError{"pkill: -n and -o are mutually exclusive"});
  }

  for (auto occ : ctx.string_occurrences({"-u", "--euid"})) {
    auto items = sel::split_list(occ.value);
    if (!items) {
      return std::unexpected(
          ConfigError{"pkill: invalid user list: '" + occ.value + "'"});
    }
    crit.euid_specs.insert(crit.euid_specs.end(), items->begin(), items->end());
  }
  for (auto occ : ctx.string_occurrences({"-U", "--uid"})) {
    auto items = sel::split_list(occ.value);
    if (!items) {
      return std::unexpected(
          ConfigError{"pkill: invalid user list: '" + occ.value + "'"});
    }
    crit.ruid_specs.insert(crit.ruid_specs.end(), items->begin(), items->end());
  }
  for (auto occ : ctx.string_occurrences({"-G", "--group"})) {
    auto items = sel::split_list(occ.value);
    if (!items) {
      return std::unexpected(
          ConfigError{"pkill: invalid group list: '" + occ.value + "'"});
    }
    crit.group_specs.insert(crit.group_specs.end(), items->begin(),
                            items->end());
  }
  std::string bad_spec;
  if (!sel::validate_account_specs(crit.euid_specs, bad_spec) ||
      !sel::validate_account_specs(crit.ruid_specs, bad_spec)) {
    return std::unexpected(
        ConfigError{"pkill: invalid user name: " + bad_spec});
  }
  if (!sel::validate_account_specs(crit.group_specs, bad_spec)) {
    return std::unexpected(
        ConfigError{"pkill: invalid group name: " + bad_spec});
  }
  for (auto occ : ctx.string_occurrences({"-P", "--parent"})) {
    if (!sel::parse_ulong_list(occ.value, crit.ppids)) {
      return std::unexpected(
          ConfigError{"pkill: invalid parent list: '" + occ.value + "'"});
    }
  }
  for (auto occ : ctx.string_occurrences({"-s", "--session"})) {
    if (!sel::parse_ulong_list(occ.value, crit.sessions)) {
      return std::unexpected(
          ConfigError{"pkill: invalid session list: '" + occ.value + "'"});
    }
  }
  if (std::ranges::find(crit.sessions, 0UL) != crit.sessions.end()) {
    DWORD own_session = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &own_session)) {
      std::ranges::replace(crit.sessions, 0UL,
                           static_cast<unsigned long>(own_session));
    }
  }
  for (auto occ : ctx.string_occurrences({"-O", "--older"})) {
    crit.older_than = std::atol(occ.value.c_str());
  }
  for (auto occ : ctx.string_occurrences({"-F", "--pidfile"})) {
    cfg.has_pidfile = true;
    cfg.pidfile_path = occ.value;
  }

  auto signal = resolve_signal(ctx);
  if (!signal) return std::unexpected(signal.error());
  cfg.signal = *signal;

  if (ctx.positionals.size() > 1) {
    return std::unexpected(
        ConfigError{"pkill: only one pattern can be provided"});
  }
  if (ctx.positionals.size() == 1) {
    crit.has_pattern = true;
    crit.pattern_text = std::string(ctx.positionals[0]);
    crit.pattern = sel::compile_pattern(utf8_to_wstring(crit.pattern_text),
                                        exact, ignore_case);
    if (!crit.pattern) {
      return std::unexpected(
          ConfigError{"pkill: invalid regular expression", 2});
    }
  }
  return cfg;
}

auto run(const Config& cfg, const std::vector<sel::Selected>& selected) -> int {
  int kill_count = 0;
  for (const auto& entry : selected) {
    const auto* proc = entry.proc;
    auto result = cfg.signal == 0 ? win32_check_process(proc->pid)
                                  : win32_terminate_process(proc->pid);
    if (result.ok) {
      if (cfg.echo) {
        // pgrep.c: "%s killed (pid %lu)" with the process name first.
        safePrintLn(wstring_to_utf8(proc->name) + " killed (pid " +
                    std::to_string(proc->pid) + ")");
      }
      ++kill_count;
      continue;
    }
    if (result.error == ERROR_INVALID_PARAMETER) {
      // The process vanished between enumeration and signaling:
      // ESRCH upstream, silently fine and not counted.
      continue;
    }
    safeErrorPrintLn("pkill: killing pid " + std::to_string(proc->pid) +
                     " failed: " + result.message);
  }
  if (cfg.count) {
    // pgrep.c prints the number of matched processes, after signaling.
    safePrintLn(std::to_string(selected.size()));
  }
  return kill_count > 0 ? 0 : 1;
}

}  // namespace pkill_pipeline

REGISTER_COMMAND(pkill, "pkill", "pkill [OPTION]... PATTERN",
                 "Signal processes selected by name or command line.\n"
                 "On Windows, TERM/KILL are mapped to Win32 termination.",
                 "  pkill -f worker-token\n"
                 "  pkill -0 explorer\n"
                 "  pkill -u 1000 -n node",
                 "pgrep(1), kill(1), killall(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PKILL_OPTIONS) {
  using namespace pkill_pipeline;

  auto built = build_config(ctx);
  if (!built) {
    safeErrorPrintLn(built.error().message);
    safeErrorPrintLn("Try `pkill --help' for more information.");
    return built.error().exit_code;
  }
  Config cfg = std::move(*built);
  auto& crit = cfg.criteria;

  if (cfg.logpidfile && !cfg.has_pidfile) {
    safeErrorPrintLn("pkill: -L without -F makes no sense");
    safeErrorPrintLn("Try `pkill --help' for more information.");
    return 2;
  }
  if (cfg.has_pidfile) {
    unsigned long pid = 0;
    if (!sel::read_pidfile(cfg.pidfile_path, cfg.logpidfile, pid)) {
      safeErrorPrintLn("pkill: pidfile not valid");
      safeErrorPrintLn("Try `pkill --help' for more information.");
      return 1;
    }
    crit.has_pidfile_pid = true;
    crit.pidfile_pid = pid;
  }
  if (!crit.has_criteria()) {
    safeErrorPrintLn("pkill: no matching criteria specified");
    safeErrorPrintLn("Try `pkill --help' for more information.");
    return 2;
  }

  auto processes = enumerate_win32_processes(crit.full);
  auto selected = sel::select_processes(crit, processes, sel::now_filetime());
  if (selected.empty() && crit.has_pattern && !crit.full &&
      sel::is_long_match(crit.pattern_text)) {
    safeErrorPrintLn(
        "pkill: pattern that searches for process name longer than 15 "
        "characters will result in zero matches");
    safeErrorPrintLn(
        "Try `pkill -f' option to match against the complete command "
        "line.");
  }
  return run(cfg, selected);
}
