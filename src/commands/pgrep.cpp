// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pgrep (procps 4.0.4 semantics; the
///     selection engine lives in procps_select.h).
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

auto constexpr PGREP_OPTIONS = std::array{
    OPTION("-a", "--list-full", "list PID and full command line"),
    OPTION("-A", "--ignore-ancestors",
           "ignore the ancestors of the pgrep process"),
    OPTION("-c", "--count", "print only a count of matching processes"),
    OPTION("-d", "--delimiter", "specify output delimiter", STRING_TYPE),
    OPTION("-f", "--full", "match against full command line"),
    OPTION("-F", "--pidfile", "read PID from file", STRING_TYPE),
    OPTION("-G", "--group", "match by real group ID or name", STRING_TYPE),
    OPTION("-i", "--ignore-case", "match case insensitively"),
    OPTION("-l", "--list-name", "list PID and process name"),
    OPTION("-L", "--logpidfile", "fail if the pidfile is not locked"),
    OPTION("-n", "--newest", "select the most recently started process"),
    OPTION("-o", "--oldest", "select the oldest process"),
    OPTION("-O", "--older", "select processes older than the given seconds",
           STRING_TYPE),
    OPTION("-P", "--parent", "match by parent PID", STRING_TYPE),
    OPTION("-s", "--session", "match by session ID (0 means own session)",
           STRING_TYPE),
    OPTION("-u", "--euid", "match by effective user ID or name", STRING_TYPE),
    OPTION("-U", "--uid", "match by real user ID or name", STRING_TYPE),
    OPTION("-v", "--inverse", "negate the matching"),
    OPTION("-w", "--lightweight", "list thread IDs of matching processes"),
    OPTION("-x", "--exact", "match the whole name or command line"),
    // Accepted for procps compatibility: in pgrep the signal is only
    // consumed by --require-handler, which has no Windows equivalent,
    // so the value is validated and otherwise unused (as upstream).
    OPTION("", "--signal", "", STRING_TYPE)};

namespace pgrep_pipeline {
namespace sel = procps_select;

struct Config {
  sel::Criteria criteria;
  bool list_name = false;
  bool list_full = false;
  bool count = false;
  bool threads = false;
  std::string delimiter = "\n";
  bool has_pidfile = false;
  std::string pidfile_path;
  bool logpidfile = false;
};

struct ConfigError {
  std::string message;
  int exit_code = 2;
};

auto build_config(const CommandContext<PGREP_OPTIONS.size()>& ctx)
    -> std::expected<Config, ConfigError> {
  Config cfg;
  auto& crit = cfg.criteria;
  crit.full = ctx.get<bool>("-f", false) || ctx.get<bool>("--full", false);
  const bool ignore_case =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--ignore-case", false);
  const bool exact =
      ctx.get<bool>("-x", false) || ctx.get<bool>("--exact", false);
  cfg.list_name =
      ctx.get<bool>("-l", false) || ctx.get<bool>("--list-name", false);
  cfg.list_full =
      ctx.get<bool>("-a", false) || ctx.get<bool>("--list-full", false);
  cfg.count = ctx.get<bool>("-c", false) || ctx.get<bool>("--count", false);
  cfg.threads =
      ctx.get<bool>("-w", false) || ctx.get<bool>("--lightweight", false);
  crit.negate = ctx.get<bool>("-v", false) || ctx.get<bool>("--inverse", false);
  crit.ignore_ancestors =
      ctx.get<bool>("-A", false) || ctx.get<bool>("--ignore-ancestors", false);
  crit.newest = ctx.get<bool>("-n", false) || ctx.get<bool>("--newest", false);
  crit.oldest = ctx.get<bool>("-o", false) || ctx.get<bool>("--oldest", false);
  cfg.logpidfile =
      ctx.get<bool>("-L", false) || ctx.get<bool>("--logpidfile", false);

  // pgrep.c: -n, -o and -v reject any combination, including repeats.
  if (ctx.count({"-n", "--newest", "-o", "--oldest", "-v", "--inverse"}) > 1) {
    return std::unexpected(
        ConfigError{"pgrep: -n, -o and -v are mutually exclusive"});
  }

  if (ctx.has("-d") || ctx.has("--delimiter")) {
    cfg.delimiter = ctx.get<std::string>("--delimiter", "\n");
  }

  // List-valued selectors. Repeated occurrences accumulate, as with
  // upstream split_list() on each argument.
  for (auto occ : ctx.string_occurrences({"-u", "--euid"})) {
    auto items = sel::split_list(occ.value);
    if (!items) {
      return std::unexpected(
          ConfigError{"pgrep: invalid user list: '" + occ.value + "'"});
    }
    crit.euid_specs.insert(crit.euid_specs.end(), items->begin(), items->end());
  }
  for (auto occ : ctx.string_occurrences({"-U", "--uid"})) {
    auto items = sel::split_list(occ.value);
    if (!items) {
      return std::unexpected(
          ConfigError{"pgrep: invalid user list: '" + occ.value + "'"});
    }
    crit.ruid_specs.insert(crit.ruid_specs.end(), items->begin(), items->end());
  }
  for (auto occ : ctx.string_occurrences({"-G", "--group"})) {
    auto items = sel::split_list(occ.value);
    if (!items) {
      return std::unexpected(
          ConfigError{"pgrep: invalid group list: '" + occ.value + "'"});
    }
    crit.group_specs.insert(crit.group_specs.end(), items->begin(),
                            items->end());
  }
  std::string bad_spec;
  if (!sel::validate_account_specs(crit.euid_specs, bad_spec) ||
      !sel::validate_account_specs(crit.ruid_specs, bad_spec)) {
    return std::unexpected(
        ConfigError{"pgrep: invalid user name: " + bad_spec});
  }
  if (!sel::validate_account_specs(crit.group_specs, bad_spec)) {
    return std::unexpected(
        ConfigError{"pgrep: invalid group name: " + bad_spec});
  }
  for (auto occ : ctx.string_occurrences({"-P", "--parent"})) {
    if (!sel::parse_ulong_list(occ.value, crit.ppids)) {
      return std::unexpected(
          ConfigError{"pgrep: invalid parent list: '" + occ.value + "'"});
    }
  }
  for (auto occ : ctx.string_occurrences({"-s", "--session"})) {
    if (!sel::parse_ulong_list(occ.value, crit.sessions)) {
      return std::unexpected(
          ConfigError{"pgrep: invalid session list: '" + occ.value + "'"});
    }
  }
  // conv_sid upstream: a session id of 0 names the caller's session.
  if (std::ranges::find(crit.sessions, 0UL) != crit.sessions.end()) {
    DWORD own_session = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &own_session)) {
      std::ranges::replace(crit.sessions, 0UL,
                           static_cast<unsigned long>(own_session));
    }
  }
  for (auto occ : ctx.string_occurrences({"-O", "--older"})) {
    // pgrep.c parses -O with atoi(): leading integer prefix, no error.
    crit.older_than = std::atol(occ.value.c_str());
  }
  for (auto occ : ctx.string_occurrences({"-F", "--pidfile"})) {
    cfg.has_pidfile = true;
    cfg.pidfile_path = occ.value;
  }
  if (auto signal = ctx.get<std::string>("--signal", "");
      !signal.empty() || ctx.has("--signal")) {
    if (sel::signal_name_to_number(signal) < 0) {
      return std::unexpected(
          ConfigError{"pgrep: invalid signal: '" + signal + "'"});
    }
  }

  if (ctx.positionals.size() > 1) {
    return std::unexpected(
        ConfigError{"pgrep: only one pattern can be provided"});
  }
  if (ctx.positionals.size() == 1) {
    crit.has_pattern = true;
    crit.pattern_text = std::string(ctx.positionals[0]);
    crit.pattern = sel::compile_pattern(utf8_to_wstring(crit.pattern_text),
                                        exact, ignore_case);
    if (!crit.pattern) {
      return std::unexpected(
          ConfigError{"pgrep: invalid regular expression", 2});
    }
  }
  return cfg;
}

auto run(const Config& cfg, const std::vector<sel::Selected>& selected) -> int {
  const bool matched = !selected.empty();

  if (cfg.count) {
    size_t total = selected.size();
    if (cfg.threads) {
      total = 0;
      for (const auto& entry : selected) {
        total += win32_process_thread_ids(entry.proc->pid).size();
      }
    }
    safePrintLn(std::to_string(total));
    return matched ? 0 : 1;
  }

  std::string out;
  auto append_entry = [&](std::string text, bool last) {
    out += text;
    out += last ? "\n" : cfg.delimiter;
  };
  if (cfg.threads) {
    std::vector<std::pair<unsigned long, const Win32ProcessInfo*>> rows;
    for (const auto& entry : selected) {
      for (DWORD tid : win32_process_thread_ids(entry.proc->pid)) {
        rows.emplace_back(tid, entry.proc);
      }
    }
    for (size_t i = 0; i < rows.size(); ++i) {
      std::string text = std::to_string(rows[i].first);
      if (cfg.list_full) {
        text += " " + wstring_to_utf8(rows[i].second->command_line);
      } else if (cfg.list_name) {
        text += " " + wstring_to_utf8(rows[i].second->name);
      }
      append_entry(std::move(text), i + 1 == rows.size());
    }
  } else {
    for (size_t i = 0; i < selected.size(); ++i) {
      const auto* proc = selected[i].proc;
      std::string text = std::to_string(proc->pid);
      if (cfg.list_full) {
        text += " " + wstring_to_utf8(proc->command_line);
      } else if (cfg.list_name) {
        text += " " + wstring_to_utf8(proc->name);
      }
      append_entry(std::move(text), i + 1 == selected.size());
    }
  }
  if (!out.empty()) safePrint(out);
  return matched ? 0 : 1;
}

}  // namespace pgrep_pipeline

REGISTER_COMMAND(pgrep, "pgrep", "pgrep [OPTION]... PATTERN",
                 "Look up processes by name or command line.\n"
                 "The current pgrep process is excluded from matches.",
                 "  pgrep explorer\n"
                 "  pgrep -af powershell\n"
                 "  pgrep -u 1000 -n node",
                 "ps(1), pkill(1), pidof(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PGREP_OPTIONS) {
  using namespace pgrep_pipeline;

  auto built = build_config(ctx);
  if (!built) {
    safeErrorPrintLn(built.error().message);
    safeErrorPrintLn("Try `pgrep --help' for more information.");
    return built.error().exit_code;
  }
  Config cfg = std::move(*built);
  auto& crit = cfg.criteria;

  if (cfg.logpidfile && !cfg.has_pidfile) {
    safeErrorPrintLn("pgrep: -L without -F makes no sense");
    safeErrorPrintLn("Try `pgrep --help' for more information.");
    return 2;
  }
  if (cfg.has_pidfile) {
    unsigned long pid = 0;
    if (!sel::read_pidfile(cfg.pidfile_path, cfg.logpidfile, pid)) {
      safeErrorPrintLn("pgrep: pidfile not valid");
      safeErrorPrintLn("Try `pgrep --help' for more information.");
      return 1;
    }
    crit.has_pidfile_pid = true;
    crit.pidfile_pid = pid;
  }
  if (!crit.has_criteria()) {
    safeErrorPrintLn("pgrep: no matching criteria specified");
    safeErrorPrintLn("Try `pgrep --help' for more information.");
    return 2;
  }

  auto processes = enumerate_win32_processes(crit.full || cfg.list_full);
  auto selected = sel::select_processes(crit, processes, sel::now_filetime());
  if (selected.empty() && crit.has_pattern && !crit.full &&
      sel::is_long_match(crit.pattern_text)) {
    safeErrorPrintLn(
        "pgrep: pattern that searches for process name longer than 15 "
        "characters will result in zero matches");
    safeErrorPrintLn(
        "Try `pgrep -f' option to match against the complete command "
        "line.");
  }
  return run(cfg, selected);
}
