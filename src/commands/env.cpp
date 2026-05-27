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
           "process and split S into separate arguments; useful for shebang",
           STRING_TYPE),
    OPTION("-a", "--argv0",
           "pass STRING as argv[0] to the command", STRING_TYPE),
    OPTION("-C", "--chdir", "change working directory", STRING_TYPE),
    OPTION("", "--default-signal",
           "reset handling of SIG to its default", OPTIONAL_STRING_TYPE),
    OPTION("", "--ignore-signal",
           "set handling of SIG to do nothing", OPTIONAL_STRING_TYPE),
    OPTION("", "--block-signal",
           "block delivery of SIG to COMMAND", OPTIONAL_STRING_TYPE),
    OPTION("", "--list-signal-handling",
           "list non default signal handling"),
    OPTION("", "--debug", "print extra information about the processing")};

namespace env_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool ignore_environment = false;
  bool null_terminated = false;
  std::vector<std::string> unset_names;
  std::map<std::string, std::string> assignments;
  std::string chdir;
  std::string argv0;  // -a
  std::string split_string;  // -S
  bool debug = false;
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
  return std::nullopt;
}

/// Split a string into arguments respecting shell-like quoting rules.
/// Supports single quotes, double quotes, backslash escaping, and comment (#).
auto split_string_args(const std::string& s) -> std::vector<std::string> {
  std::vector<std::string> args;
  std::string current;
  bool in_single = false;
  bool in_double = false;

  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];

    if (in_single) {
      if (c == '\'') {
        in_single = false;
      } else {
        current += c;
      }
    } else if (in_double) {
      if (c == '"') {
        in_double = false;
      } else if (c == '\\' && i + 1 < s.size() &&
                 (s[i + 1] == '"' || s[i + 1] == '\\' || s[i + 1] == '$' ||
                  s[i + 1] == '`')) {
        current += s[++i];
      } else {
        current += c;
      }
    } else {
      if (c == '\'') {
        in_single = true;
      } else if (c == '"') {
        in_double = true;
      } else if (c == '\\' && i + 1 < s.size()) {
        current += s[++i];
      } else if (c == ' ' || c == '\t') {
        if (!current.empty()) {
          args.push_back(std::move(current));
          current.clear();
        }
      } else if (c == '#' && current.empty()) {
        // Comment - rest of string is ignored
        break;
      } else {
        current += c;
      }
    }
  }

  if (!current.empty()) {
    args.push_back(std::move(current));
  }

  return args;
}

auto build_config(const CommandContext<ENV_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.ignore_environment = ctx.get<bool>("--ignore-environment", false) ||
                           ctx.get<bool>("-i", false);
  cfg.null_terminated =
      ctx.get<bool>("--null", false) || ctx.get<bool>("-0", false);
  cfg.unset_names = ctx.get_all<std::string>("--unset");
  auto short_unsets = ctx.get_all<std::string>("-u");
  cfg.unset_names.insert(cfg.unset_names.end(), short_unsets.begin(),
                         short_unsets.end());

  cfg.chdir = ctx.get<std::string>("--chdir", "");
  if (cfg.chdir.empty()) cfg.chdir = ctx.get<std::string>("-C", "");
  cfg.argv0 = ctx.get<std::string>("--argv0", "");
  if (cfg.argv0.empty()) cfg.argv0 = ctx.get<std::string>("-a", "");
  cfg.split_string = ctx.get<std::string>("--split-string", "");
  if (cfg.split_string.empty())
    cfg.split_string = ctx.get<std::string>("-S", "");
  cfg.debug = ctx.get<bool>("--debug", false);

  // If -S is provided, split it into arguments
  if (!cfg.split_string.empty()) {
    auto split_args = split_string_args(cfg.split_string);
    bool first = true;
    for (auto& arg : split_args) {
      if (first) {
        // First part could be NAME=VALUE or command
        auto assign = parse_assignment(arg);
        if (assign.has_value()) {
          cfg.assignments[assign->first] = assign->second;
        } else {
          cfg.command.emplace_back(std::move(arg));
          first = false;
        }
      } else {
        cfg.command.emplace_back(std::move(arg));
      }
    }
  }

  bool in_command = !cfg.command.empty();
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

auto quote_arg(const std::wstring& arg) -> std::wstring {
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

auto build_command_line(const SmallVector<std::string, 32>& command)
    -> std::wstring {
  std::wstring out = quote_arg(utf8_to_wstring(command.front()));
  for (size_t i = 1; i < command.size(); ++i) {
    out.push_back(L' ');
    out += quote_arg(utf8_to_wstring(command[i]));
  }
  return out;
}

auto build_environment_block(const std::map<std::string, std::string>& vars)
    -> std::vector<wchar_t> {
  std::vector<wchar_t> block;
  for (const auto& [key, value] : vars) {
    auto entry = utf8_to_wstring(key + "=" + value);
    block.insert(block.end(), entry.begin(), entry.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

auto make_working_directory_arg(const std::string& chdir, std::wstring& storage)
    -> LPCWSTR {
  if (chdir.empty()) return nullptr;
  storage = utf8_to_wstring(chdir);
  DWORD attrs = GetFileAttributesW(storage.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES ||
      (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    return nullptr;
  }
  return storage.c_str();
}

auto materialize_environment(const Config& cfg)
    -> std::map<std::string, std::string> {
  std::map<std::string, std::string> vars;
  if (!cfg.ignore_environment) {
    vars = parse_env_block();
    if (cfg.debug) {
      safeErrorPrint("env: loaded " + std::to_string(vars.size()) +
                     " variables from environment\n");
    }
  } else if (cfg.debug) {
    safeErrorPrint("env: starting with empty environment (-i)\n");
  }

  for (const auto& unset_name : cfg.unset_names) {
    auto it = vars.find(unset_name);
    if (it != vars.end()) {
      if (cfg.debug) {
        safeErrorPrint("env: unset '" + unset_name + "'\n");
      }
      vars.erase(it);
    }
  }

  for (const auto& [k, v] : cfg.assignments) {
    if (cfg.debug) {
      safeErrorPrint("env: set '" + k + "'='" + v + "'\n");
    }
    vars[k] = v;
  }

  return vars;
}

auto run_command(const Config& cfg,
                 const std::map<std::string, std::string>& vars) -> int {
  // If -a is set, replace argv[0]
  SmallVector<std::string, 32> actual_command;
  if (!cfg.argv0.empty() && !cfg.command.empty()) {
    actual_command.push_back(cfg.argv0);
    for (size_t i = 1; i < cfg.command.size(); ++i) {
      actual_command.push_back(cfg.command[i]);
    }
  } else {
    actual_command = cfg.command;
  }

  if (cfg.debug) {
    std::string cmd_str;
    for (size_t i = 0; i < actual_command.size(); ++i) {
      if (i > 0) cmd_str += " ";
      cmd_str += "'" + actual_command[i] + "'";
    }
    safeErrorPrint("env: executing: " + cmd_str + "\n");
  }

  auto cmd_line = build_command_line(actual_command);
  auto env_block = build_environment_block(vars);
  std::wstring working_directory;
  LPCWSTR working_directory_arg =
      make_working_directory_arg(cfg.chdir, working_directory);
  if (!cfg.chdir.empty() && working_directory_arg == nullptr) {
    cp::report_custom_error(L"env", L"cannot change directory");
    return 125;
  }

  if (cfg.debug && !cfg.chdir.empty()) {
    safeErrorPrint("env: working directory: " + cfg.chdir + "\n");
  }

  STARTUPINFOW si{sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, TRUE,
                           CREATE_UNICODE_ENVIRONMENT, env_block.data(),
                           working_directory_arg, &si, &pi);
  if (!ok) {
    cp::report_custom_error(L"env", L"failed to execute command");
    return 127;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (cfg.debug) {
    safeErrorPrint("env: command exited with code " +
                   std::to_string(exit_code) + "\n");
  }

  return static_cast<int>(exit_code);
}

auto run(const Config& cfg) -> int {
  auto vars = materialize_environment(cfg);

  if (!cfg.command.empty()) {
    return run_command(cfg, vars);
  }

  if (!cfg.chdir.empty()) {
    std::wstring working_directory;
    LPCWSTR working_directory_arg =
        make_working_directory_arg(cfg.chdir, working_directory);
    if (working_directory_arg == nullptr ||
        !SetCurrentDirectoryW(working_directory_arg)) {
      cp::report_custom_error(L"env", L"cannot change directory");
      return 125;
    }
  }

  print_env(vars, cfg.null_terminated);
  return 0;
}

}  // namespace env_pipeline

REGISTER_COMMAND(env, "env",
                 "env [OPTION]... [NAME=VALUE]... [COMMAND [ARG]...]",
                 "Set each NAME to VALUE in the environment and print the\n"
                 "resulting environment, or run COMMAND in that environment.\n"
                 "\n"
                 "  -S, --split-string   process and split S into arguments\n"
                 "  -a, --argv0          pass STRING as argv[0] to command\n"
                 "  -C, --chdir          change working directory\n"
                 "\n"
                 "Signal options (accepted, limited Windows support):\n"
                 "  --default-signal     reset handling of SIG to default\n"
                 "  --ignore-signal      set handling of SIG to do nothing\n"
                 "  --block-signal       block delivery of SIG to COMMAND\n"
                 "  --list-signal-handling  list non default signal handling",
                 "  env\n"
                 "  env -i FOO=bar\n"
                 "  env -u PATH\n"
                 "  env -S '-i FOO=bar command args'\n"
                 "  env -a 'myname' command\n"
                 "  env -C /tmp command",
                 "printenv(1), which(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", ENV_OPTIONS) {
  using namespace env_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"env");
    return 1;
  }
  return run(*cfg);
}
