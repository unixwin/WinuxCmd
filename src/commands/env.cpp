/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implemention for env.
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

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr ENV_OPTIONS = std::array{
    OPTION("-i", "--ignore-environment", "start with an empty environment"),
    OPTION("-u", "--unset", "remove variable from the environment",
           STRING_TYPE),
    OPTION("-0", "--null", "end each output line with NUL, not newline"),
    OPTION("-S", "--split-string",
           "process and split S into separate arguments [NOT SUPPORT]",
           STRING_TYPE),
    OPTION("-C", "--chdir", "change working directory [NOT SUPPORT]",
           STRING_TYPE)};

namespace env_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool ignore_environment = false;
  bool null_terminated = false;
  std::string unset_name;
  std::map<std::string, std::string> assignments;
  SmallVector<std::string, 32> command;
};

auto parse_env_block() -> std::map<std::string, std::string> {
  std::map<std::string, std::string> out;
  LPWCH block = GetEnvironmentStringsW();
  if (block == nullptr) return out;

  const wchar_t* p = block;
  while (*p != L'\0') {
    std::wstring entry = p;
    p += entry.size() + 1;

    if (entry.empty()) continue;
    if (entry[0] == L'=') continue;

    auto pos = entry.find(L'=');
    if (pos == std::wstring::npos) continue;

    auto key = wstring_to_utf8(entry.substr(0, pos));
    auto value = wstring_to_utf8(entry.substr(pos + 1));
    out[std::move(key)] = std::move(value);
  }

  FreeEnvironmentStringsW(block);
  return out;
}

auto parse_assignment(std::string_view s)
    -> std::optional<std::pair<std::string, std::string>> {
  auto eq = s.find('=');
  if (eq == std::string_view::npos || eq == 0) return std::nullopt;
  return std::pair{std::string(s.substr(0, eq)), std::string(s.substr(eq + 1))};
}

auto is_unsupported_used(const CommandContext<ENV_OPTIONS.size()>& ctx)
    -> std::optional<std::string_view> {
  if (!ctx.get<std::string>("--split-string", "").empty() ||
      !ctx.get<std::string>("-S", "").empty()) {
    return "--split-string is [NOT SUPPORT]";
  }
  if (!ctx.get<std::string>("--chdir", "").empty() ||
      !ctx.get<std::string>("-C", "").empty()) {
    return "--chdir is [NOT SUPPORT]";
  }
  return std::nullopt;
}

auto build_config(const CommandContext<ENV_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.ignore_environment = ctx.get<bool>("--ignore-environment", false) ||
                           ctx.get<bool>("-i", false);
  cfg.null_terminated =
      ctx.get<bool>("--null", false) || ctx.get<bool>("-0", false);
  cfg.unset_name = ctx.get<std::string>("--unset", "");
  if (cfg.unset_name.empty()) cfg.unset_name = ctx.get<std::string>("-u", "");

  bool in_command = false;
  for (auto p : ctx.positionals) {
    if (in_command) {
      cfg.command.emplace_back(p);
      continue;
    }

    auto assign = parse_assignment(p);
    if (assign.has_value()) {
      cfg.assignments[assign->first] = assign->second;
      continue;
    }

    in_command = true;
    cfg.command.emplace_back(p);
  }

  return cfg;
}

auto print_env(const std::map<std::string, std::string>& vars,
               bool null_terminated) -> void {
  for (const auto& [k, v] : vars) {
    safePrint(k);
    safePrint("=");
    safePrint(v);
    if (null_terminated) {
      safePrint(char{'\0'});
    } else {
      safePrint("\n");
    }
  }
}

auto run(const Config& cfg) -> int {
  if (!cfg.command.empty()) {
    cp::report_custom_error(L"env", L"running command is [NOT SUPPORT]");
    return 2;
  }

  std::map<std::string, std::string> vars;
  if (!cfg.ignore_environment) {
    vars = parse_env_block();
  }

  if (!cfg.unset_name.empty()) {
    auto it = vars.find(cfg.unset_name);
    if (it != vars.end()) {
      vars.erase(it);
    }
  }

  for (const auto& [k, v] : cfg.assignments) {
    vars[k] = v;
  }

  print_env(vars, cfg.null_terminated);
  return 0;
}

}  // namespace env_pipeline

REGISTER_COMMAND(
    env, "env", "env [OPTION]... [NAME=VALUE]... [COMMAND [ARG]...]",
    "Set each NAME to VALUE in the environment and print the\n"
    "resulting environment. Running COMMAND is currently [NOT SUPPORT].",
    "  env\n"
    "  env -i FOO=bar\n"
    "  env -u PATH",
    "printenv(1), which(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    ENV_OPTIONS) {
  using namespace env_pipeline;

  if (auto unsupported = is_unsupported_used(ctx); unsupported.has_value()) {
    cp::report_custom_error(L"env", utf8_to_wstring(*unsupported));
    return 2;
  }

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"env");
    return 1;
  }
  return run(*cfg);
}
