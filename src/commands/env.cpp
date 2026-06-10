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
    OPTION("-f", "--file",
           "read and set variables from a .env-style configuration file",
           STRING_TYPE),
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
    OPTION("-v", "--debug", "print extra information about the processing")};

namespace env_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool ignore_environment = false;
  bool null_terminated = false;
  std::vector<std::string> unset_names;
  std::map<std::string, std::string> file_assignments;
  std::map<std::string, std::string> assignments;
  std::string env_file;
  std::string env_file_error;
  std::string chdir;
  std::string argv0;  // -a
  std::string split_string;  // -S
  size_t debug_level = 0;
  bool debug = false;
  SmallVector<std::string, 32> command;
  SmallVector<std::string, 32> raw_args;
};

auto split_semicolon(std::string_view text) -> std::vector<std::string> {
  SmallVector<std::string, 256> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t pos = text.find(';', start);
    if (pos == std::string_view::npos) {
      out.emplace_back(text.substr(start));
      break;
    }
    out.emplace_back(text.substr(start, pos - start));
    start = pos + 1;
  }
  return std::vector<std::string>(out.begin(), out.end());
}

auto get_env_utf8(const wchar_t* key) -> std::optional<std::string> {
  DWORD size = GetEnvironmentVariableW(key, nullptr, 0);
  if (size == 0) return std::nullopt;

  std::wstring value;
  value.resize(size - 1);
  if (GetEnvironmentVariableW(key, value.data(), size) == 0) {
    return std::nullopt;
  }
  return wstring_to_utf8(value);
}

auto get_path_entries() -> std::vector<std::string> {
  auto path_env = get_env_utf8(L"PATH");
  if (!path_env.has_value() || path_env->empty()) return {};
  return split_semicolon(*path_env);
}

auto get_pathext_entries() -> std::vector<std::string> {
  auto ext_env = get_env_utf8(L"PATHEXT");
  if (!ext_env.has_value() || ext_env->empty()) {
    return std::vector<std::string>{".exe", ".cmd", ".bat", ".com"};
  }
  auto exts = split_semicolon(*ext_env);
  if (exts.empty()) {
    exts = std::vector<std::string>{".exe", ".cmd", ".bat", ".com"};
  }
  for (auto& ext : exts) {
    if (!ext.empty() && ext.front() != '.') ext.insert(ext.begin(), '.');
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
  }
  return exts;
}

auto has_path_separator(std::string_view s) -> bool {
  return s.find('/') != std::string_view::npos ||
         s.find('\\') != std::string_view::npos;
}

auto exists_regular(const std::filesystem::path& p) -> bool {
  std::error_code ec;
  return std::filesystem::exists(p, ec) &&
         std::filesystem::is_regular_file(p, ec);
}

auto exists_any(const std::filesystem::path& p) -> bool {
  std::error_code ec;
  return std::filesystem::exists(p, ec);
}

auto with_extensions(const std::filesystem::path& base,
                     const std::vector<std::string>& exts)
    -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> out;
  out.push_back(base);
  if (base.has_extension()) return out;

  for (const auto& ext : exts) {
    auto candidate = base;
    candidate += ext;
    out.push_back(std::move(candidate));
  }
  return out;
}

enum class ExecutableLookupStatus {
  Found,
  NotFound,
  NotExecutable,
};

struct ExecutableLookupResult {
  ExecutableLookupStatus status = ExecutableLookupStatus::NotFound;
  std::optional<std::wstring> path;
};

auto resolve_executable(std::string_view program,
                        std::string_view working_directory = {})
    -> ExecutableLookupResult {
  const auto pathext = get_pathext_entries();
  bool found_non_executable = false;

  auto scan_candidates =
      [&](const std::filesystem::path& base) -> std::optional<std::wstring> {
    for (const auto& candidate : with_extensions(base, pathext)) {
      if (exists_regular(candidate)) return candidate.wstring();
      if (exists_any(candidate)) found_non_executable = true;
    }
    return std::nullopt;
  };

  if (has_path_separator(program)) {
    std::filesystem::path base{std::string(program)};
    if (auto match = scan_candidates(base)) {
      return {ExecutableLookupStatus::Found, std::move(match)};
    }
    return {found_non_executable ? ExecutableLookupStatus::NotExecutable
                                 : ExecutableLookupStatus::NotFound,
            std::nullopt};
  }

  std::vector<std::filesystem::path> search_roots;
  if (!working_directory.empty()) {
    search_roots.emplace_back(std::string(working_directory));
  } else {
    search_roots.emplace_back(std::filesystem::current_path());
  }

  for (const auto& root : search_roots) {
    std::filesystem::path base = root / std::string(program);
    if (auto match = scan_candidates(base)) {
      return {ExecutableLookupStatus::Found, std::move(match)};
    }
  }

  for (const auto& dir : get_path_entries()) {
    if (dir.empty()) continue;
    std::filesystem::path base = std::filesystem::path(dir) / std::string(program);
    if (auto match = scan_candidates(base)) {
      return {ExecutableLookupStatus::Found, std::move(match)};
    }
  }

  return {found_non_executable ? ExecutableLookupStatus::NotExecutable
                               : ExecutableLookupStatus::NotFound,
          std::nullopt};
}

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

auto trim_ascii(std::string_view text) -> std::string_view {
  size_t start = 0;
  size_t end = text.size();
  while (start < end &&
         (text[start] == ' ' || text[start] == '\t' || text[start] == '\r')) {
    ++start;
  }
  while (end > start &&
         (text[end - 1] == ' ' || text[end - 1] == '\t' ||
          text[end - 1] == '\r')) {
    --end;
  }
  return text.substr(start, end - start);
}

auto read_env_file(std::string_view path)
    -> std::expected<std::map<std::string, std::string>, std::string> {
  auto input_open_error = [](std::string_view input_path) -> std::string {
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::u8path(input_path),
                                      ec) &&
        !ec) {
      return "cannot read env file '" + std::string(input_path) +
             "': Is a directory";
    }
    return "cannot read env file '" + std::string(input_path) +
           "': No such file or directory";
  };

  std::ifstream in(std::string(path), std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected(input_open_error(path));
  }

  std::string file_text{std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>()};
  if (file_text.size() >= 3 &&
      static_cast<unsigned char>(file_text[0]) == 0xEF &&
      static_cast<unsigned char>(file_text[1]) == 0xBB &&
      static_cast<unsigned char>(file_text[2]) == 0xBF) {
    file_text.erase(0, 3);
  }

  std::map<std::string, std::string> vars;
  std::string_view text_view{file_text};
  size_t start = 0;
  while (start <= file_text.size()) {
    size_t end = file_text.find('\n', start);
    if (end == std::string::npos) end = file_text.size();
    auto line = trim_ascii(text_view.substr(start, end - start));
    if (!line.empty() && line.front() != '#') {
      auto eq = line.find('=');
      if (eq != std::string_view::npos && eq != 0) {
        auto key = trim_ascii(line.substr(0, eq));
        auto value = trim_ascii(line.substr(eq + 1));
        if (!key.empty()) {
          vars[std::string(key)] = std::string(value);
        }
      }
    }
    if (end == file_text.size()) break;
    start = end + 1;
  }

  return vars;
}

auto is_unsupported_used(const CommandContext<ENV_OPTIONS.size()>& ctx)
    -> std::optional<std::string_view> {
  return std::nullopt;
}

/// Split a string into arguments respecting shell-like quoting rules.
/// Supports single quotes, double quotes, backslash escaping, and comment (#).
auto split_string_args(const std::string& s)
    -> cp::Result<std::vector<std::string>> {
  std::vector<std::string> args;
  std::string current;
  bool in_single = false;
  bool in_double = false;

  auto push_current = [&]() {
    if (!current.empty()) {
      args.push_back(std::move(current));
      current.clear();
    }
  };

  auto append_escape = [&](char escape, bool quoted_double)
      -> std::optional<bool> {
    switch (escape) {
      case 'c':
        if (!quoted_double) {
          return true;
        }
        current += '\\';
        current += 'c';
        return false;
      case 'f':
        current += '\f';
        return false;
      case 'n':
        current += '\n';
        return false;
      case 'r':
        current += '\r';
        return false;
      case 't':
        current += '\t';
        return false;
      case 'v':
        current += '\v';
        return false;
      case '#':
        current += '#';
        return false;
      case '$':
        current += '$';
        return false;
      case '_':
        if (quoted_double) {
          current += ' ';
        } else {
          push_current();
        }
        return false;
      case '"':
        current += '"';
        return false;
      case '\'':
        current += '\'';
        return false;
      case '\\':
        current += '\\';
        return false;
      default:
        current += escape;
        return false;
    }
  };

  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];

    if (in_single) {
      if (c == '\\' && i + 1 < s.size() &&
          (s[i + 1] == '\'' || s[i + 1] == '\\')) {
        current += s[++i];
      } else if (c == '\'') {
        in_single = false;
      } else {
        current += c;
      }
    } else if (in_double) {
      if (c == '"') {
        in_double = false;
      } else if (c == '\\' && i + 1 < s.size()) {
        auto stop = append_escape(s[++i], true);
        if (stop.value_or(false)) {
          break;
        }
      } else {
        current += c;
      }
    } else {
      if (c == '\'') {
        in_single = true;
      } else if (c == '"') {
        in_double = true;
      } else if (c == '\\' && i + 1 < s.size()) {
        auto stop = append_escape(s[++i], false);
        if (stop.value_or(false)) {
          break;
        }
      } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                 c == '\v' || c == '\f') {
        push_current();
      } else if (c == '#' && current.empty()) {
        // Comment - rest of string is ignored
        break;
      } else {
        current += c;
      }
    }
  }

  push_current();

  if (in_single || in_double) {
    return std::unexpected("no terminating quote in -S string");
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

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= ENV_OPTIONS.size()) {
      continue;
    }

    const auto& meta = (*ctx.metas)[occurrence.index];
    const auto* value = std::get_if<std::string>(&occurrence.value);
    if (value == nullptr) {
      continue;
    }

    if (meta.long_name == "--chdir" || meta.short_name == "-C") {
      cfg.chdir = *value;
      continue;
    }

    if (meta.long_name == "--file" || meta.short_name == "-f") {
      cfg.env_file = *value;
      continue;
    }

    if (meta.long_name == "--argv0" || meta.short_name == "-a") {
      cfg.argv0 = *value;
      continue;
    }

    if (meta.long_name == "--split-string" || meta.short_name == "-S") {
      cfg.split_string = *value;
    }
  }

  cfg.debug_level = ctx.count({"--debug", "-v"});
  cfg.debug = cfg.debug_level > 0;
  if (cfg.debug_level >= 2) {
    for (auto arg : ctx.raw_args) cfg.raw_args.emplace_back(arg);
  }

  if (!cfg.env_file.empty()) {
    auto file_vars = read_env_file(cfg.env_file);
    if (!file_vars) {
      cfg.env_file_error = std::move(file_vars.error());
    } else {
      cfg.file_assignments = std::move(*file_vars);
    }
  }

  // If -S is provided, split it into arguments
  if (!cfg.split_string.empty()) {
    auto split_args = split_string_args(cfg.split_string);
    if (!split_args) return std::unexpected(split_args.error());
    bool first = true;
    for (auto& arg : *split_args) {
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

auto build_command_line(const SmallVector<std::string, 32>& command,
                        const std::optional<std::string>& argv0_override =
                            std::nullopt)
    -> std::wstring {
  auto argv0 =
      argv0_override.has_value() ? utf8_to_wstring(*argv0_override)
                                 : utf8_to_wstring(command.front());
  std::wstring out = quote_arg(argv0);
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

  for (const auto& [k, v] : cfg.file_assignments) {
    if (cfg.debug) {
      safeErrorPrint("env: file set '" + k + "'='" + v + "'\n");
    }
    vars[k] = v;
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

auto env_command_status_from_create_error(DWORD error) -> int {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return 127;
    default:
      return 126;
  }
}

auto env_windows_error_text(DWORD error) -> std::string {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return "No such file or directory";
    case ERROR_ACCESS_DENIED:
    case ERROR_BAD_EXE_FORMAT:
      return "Permission denied";
    default:
      return std::system_category().message(static_cast<int>(error));
  }
}

auto run_command(const Config& cfg,
                 const std::map<std::string, std::string>& vars) -> int {
  if (cfg.debug) {
    auto arg0 = cfg.argv0.empty() ? cfg.command.front() : cfg.argv0;
    safeErrorPrint("executing: " + cfg.command.front() + "\n");
    if (!cfg.argv0.empty()) {
      safeErrorPrint("argv0:     " + cfg.argv0 + "\n");
    }
    safeErrorPrint("   arg[0]= " + arg0 + "\n");
    for (size_t i = 1; i < cfg.command.size(); ++i) {
      safeErrorPrint("   arg[" + std::to_string(i) + "]= " + cfg.command[i] +
                     "\n");
    }
  }

  std::optional<std::wstring> application_name;
  if (!cfg.argv0.empty()) {
    auto lookup = resolve_executable(cfg.command.front(), cfg.chdir);
    if (lookup.status != ExecutableLookupStatus::Found) {
      const DWORD error = lookup.status == ExecutableLookupStatus::NotExecutable
                              ? ERROR_ACCESS_DENIED
                              : ERROR_FILE_NOT_FOUND;
      safeErrorPrint("env: failed to run command '" + cfg.command.front() +
                     "': " + env_windows_error_text(error) + "\n");
      return env_command_status_from_create_error(error);
    }
    application_name = std::move(lookup.path);
  }

  auto cmd_line = build_command_line(
      cfg.command, cfg.argv0.empty() ? std::nullopt
                                     : std::optional<std::string>(cfg.argv0));
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
  BOOL ok = CreateProcessW(
      application_name.has_value() ? application_name->c_str() : nullptr,
      cmd_line.data(), nullptr, nullptr, TRUE,
                           CREATE_UNICODE_ENVIRONMENT, env_block.data(),
                           working_directory_arg, &si, &pi);
  if (!ok) {
    DWORD error = GetLastError();
    safeErrorPrint("env: failed to run command '" + cfg.command.front() +
                   "': " + env_windows_error_text(error) + "\n");
    return env_command_status_from_create_error(error);
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
  if (!cfg.env_file_error.empty()) {
    safeErrorPrint("env: " + cfg.env_file_error + "\n");
    return 1;
  }

  if (cfg.command.empty() && !cfg.chdir.empty()) {
    cp::report_custom_error(L"env", L"must specify command with --chdir");
    return 125;
  }

  if (cfg.null_terminated && !cfg.command.empty()) {
    cp::report_custom_error(L"env", L"cannot specify --null (-0) with command");
    return 125;
  }

  if (cfg.debug_level >= 2) {
    safeErrorPrint("input args:\n");
    for (size_t i = 0; i < cfg.raw_args.size(); ++i) {
      safeErrorPrint("arg[" + std::to_string(i) + "]: " + cfg.raw_args[i] +
                     "\n");
    }
  }

  auto vars = materialize_environment(cfg);

  if (!cfg.command.empty()) {
    return run_command(cfg, vars);
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
                 "  -f, --file           read variables from a .env-style file\n"
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
                 "  env -f .env command\n"
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
    return 125;
  }
  return run(*cfg);
}
