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
    // [GNU]
    OPTION("-i", "--ignore-environment", "start with an empty environment"),
    // [GNU]
    OPTION("-u", "--unset", "remove variable from the environment",
           STRING_TYPE),
    // [GNU]
    OPTION("-0", "--null", "end each output line with NUL, not newline"),
    // [EXT] -f/--file: WinuxCmd extension, not in GNU coreutils
    OPTION("-f", "--file",
           "read and set variables from a .env-style configuration file",
           STRING_TYPE),
    // [GNU]
    OPTION("-S", "--split-string",
           "process and split S into separate arguments; useful for shebang",
           STRING_TYPE),
    // [EXT] -a/--argv0: WinuxCmd extension, not in GNU coreutils
    OPTION("-a", "--argv0", "pass STRING as argv[0] to the command",
           STRING_TYPE),
    // [GNU]
    OPTION("-C", "--chdir", "change working directory", STRING_TYPE),
    // [GNU]
    OPTION("", "--default-signal", "reset handling of SIG to its default",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("", "--ignore-signal", "set handling of SIG to do nothing",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("", "--block-signal", "block delivery of SIG to COMMAND",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("", "--list-signal-handling", "list non default signal handling"),
    // [GNU]
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
  std::string argv0;         // -a
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

auto find_env_value(const std::map<std::string, std::string>& vars,
                    std::string_view key) -> std::optional<std::string> {
  auto it = vars.find(std::string(key));
  if (it != vars.end()) return it->second;

  for (const auto& [name, value] : vars) {
    if (name.size() != key.size()) continue;
    bool same = true;
    for (size_t i = 0; i < key.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(name[i])) !=
          std::tolower(static_cast<unsigned char>(key[i]))) {
        same = false;
        break;
      }
    }
    if (same) return value;
  }

  return std::nullopt;
}

auto get_path_entries(const std::map<std::string, std::string>& vars)
    -> std::vector<std::string> {
  auto path_env = find_env_value(vars, "PATH");
  if (!path_env.has_value() || path_env->empty()) return {};
  return split_semicolon(*path_env);
}

auto get_pathext_entries(const std::map<std::string, std::string>& vars)
    -> std::vector<std::string> {
  auto ext_env = find_env_value(vars, "PATHEXT");
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
  NotADirectory,
};

struct ExecutableLookupResult {
  ExecutableLookupStatus status = ExecutableLookupStatus::NotFound;
  std::optional<std::wstring> path;
};

auto resolve_executable(std::string_view program,
                        const std::map<std::string, std::string>& vars,
                        std::string_view working_directory = {})
    -> ExecutableLookupResult {
  const auto pathext = get_pathext_entries(vars);
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
    // [GNU] a trailing separator after a regular file is ENOTDIR
    // ("Not a directory", exit 126), not ENOENT (uutils #12778).
    if (program.ends_with('/') || program.ends_with('\\')) {
      std::string stripped(program);
      while (!stripped.empty() &&
             (stripped.back() == '/' || stripped.back() == '\\')) {
        stripped.pop_back();
      }
      std::error_code ec;
      std::filesystem::path stripped_path{stripped};
      if (std::filesystem::exists(stripped_path, ec) &&
          !std::filesystem::is_directory(stripped_path, ec)) {
        return {ExecutableLookupStatus::NotADirectory, std::nullopt};
      }
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

  for (const auto& dir : get_path_entries(vars)) {
    if (dir.empty()) continue;
    std::filesystem::path base =
        std::filesystem::path(dir) / std::string(program);
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
  while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
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

  auto append_escape = [&](char escape,
                           bool quoted_double) -> std::optional<bool> {
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
      } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
                 c == '\f') {
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

auto split_option_value(const std::vector<std::string>& args, size_t& index,
                        std::string_view attached, std::string_view option_name)
    -> cp::Result<std::string> {
  if (!attached.empty()) return std::string(attached);
  if (index + 1 >= args.size()) {
    return std::unexpected("option " + std::string(option_name) +
                           " requires an argument");
  }
  ++index;
  return args[index];
}

auto apply_split_arguments(Config& cfg, std::vector<std::string> split_args)
    -> cp::Result<void> {
  bool in_command = !cfg.command.empty();
  for (size_t i = 0; i < split_args.size(); ++i) {
    std::string arg = std::move(split_args[i]);
    if (in_command) {
      cfg.command.emplace_back(std::move(arg));
      continue;
    }

    if (arg == "--") {
      in_command = true;
      continue;
    }
    if (arg == "-") {
      cfg.ignore_environment = true;
      continue;
    }
    if (arg == "--ignore-environment") {
      cfg.ignore_environment = true;
      continue;
    }
    if (arg == "--null") {
      cfg.null_terminated = true;
      continue;
    }
    if (arg == "--debug") {
      ++cfg.debug_level;
      cfg.debug = true;
      continue;
    }

    auto handle_long_value = [&](std::string_view prefix,
                                 std::string_view option_name)
        -> cp::Result<std::optional<std::string>> {
      if (arg == option_name) {
        auto value = split_option_value(split_args, i, {}, option_name);
        if (!value) return std::unexpected(value.error());
        return std::optional<std::string>(*value);
      }
      if (arg.starts_with(prefix)) {
        return std::optional<std::string>(arg.substr(prefix.size()));
      }
      return std::optional<std::string>{};
    };

    auto unset_value = handle_long_value("--unset=", "--unset");
    if (!unset_value) return std::unexpected(unset_value.error());
    if (unset_value->has_value()) {
      cfg.unset_names.push_back(**unset_value);
      continue;
    }
    auto chdir_value = handle_long_value("--chdir=", "--chdir");
    if (!chdir_value) return std::unexpected(chdir_value.error());
    if (chdir_value->has_value()) {
      cfg.chdir = **chdir_value;
      continue;
    }
    auto argv0_value = handle_long_value("--argv0=", "--argv0");
    if (!argv0_value) return std::unexpected(argv0_value.error());
    if (argv0_value->has_value()) {
      cfg.argv0 = **argv0_value;
      continue;
    }
    auto file_value = handle_long_value("--file=", "--file");
    if (!file_value) return std::unexpected(file_value.error());
    if (file_value->has_value()) {
      cfg.env_file = **file_value;
      continue;
    }
    auto split_value = handle_long_value("--split-string=", "--split-string");
    if (!split_value) return std::unexpected(split_value.error());
    if (split_value->has_value()) {
      auto nested = split_string_args(**split_value);
      if (!nested) return std::unexpected(nested.error());
      split_args.insert(split_args.begin() + static_cast<std::ptrdiff_t>(i + 1),
                        std::make_move_iterator(nested->begin()),
                        std::make_move_iterator(nested->end()));
      continue;
    }

    if (arg.size() >= 2 && arg[0] == 45 && arg[1] != 45) {
      for (size_t pos = 1; pos < arg.size(); ++pos) {
        char opt = arg[pos];
        if (opt == 105) {
          cfg.ignore_environment = true;
          continue;
        }
        if (opt == 48) {
          cfg.null_terminated = true;
          continue;
        }
        if (opt == 118) {
          ++cfg.debug_level;
          cfg.debug = true;
          continue;
        }

        std::string_view attached;
        if (pos + 1 < arg.size()) {
          attached = std::string_view(arg).substr(pos + 1);
          pos = arg.size();
        }
        if (opt == 117) {
          auto value = split_option_value(split_args, i, attached, "-u");
          if (!value) return std::unexpected(value.error());
          cfg.unset_names.push_back(*value);
          break;
        }
        if (opt == 67) {
          auto value = split_option_value(split_args, i, attached, "-C");
          if (!value) return std::unexpected(value.error());
          cfg.chdir = *value;
          break;
        }
        if (opt == 97) {
          auto value = split_option_value(split_args, i, attached, "-a");
          if (!value) return std::unexpected(value.error());
          cfg.argv0 = *value;
          break;
        }
        if (opt == 102) {
          auto value = split_option_value(split_args, i, attached, "-f");
          if (!value) return std::unexpected(value.error());
          cfg.env_file = *value;
          break;
        }
        if (opt == 83) {
          auto value = split_option_value(split_args, i, attached, "-S");
          if (!value) return std::unexpected(value.error());
          auto nested = split_string_args(*value);
          if (!nested) return std::unexpected(nested.error());
          split_args.insert(
              split_args.begin() + static_cast<std::ptrdiff_t>(i + 1),
              std::make_move_iterator(nested->begin()),
              std::make_move_iterator(nested->end()));
          break;
        }
        return std::unexpected("invalid option in -S string: " + arg);
      }
      continue;
    }

    auto assign = parse_assignment(arg);
    if (assign.has_value()) {
      cfg.assignments[assign->first] = assign->second;
      continue;
    }
    in_command = true;
    cfg.command.emplace_back(std::move(arg));
  }
  return {};
}
auto build_config(const CommandContext<ENV_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.ignore_environment = ctx.get<bool>("--ignore-environment", false) ||
                           ctx.get<bool>("-i", false);
  cfg.null_terminated =
      ctx.get<bool>("--null", false) || ctx.get<bool>("-0", false);
  // [GNU] -u/--unset share one option meta: a single get_all by either
  // spelling already returns every occurrence in argv order.
  cfg.unset_names = ctx.get_all<std::string>("--unset");

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

  if (!cfg.split_string.empty()) {
    auto split_args = split_string_args(cfg.split_string);
    if (!split_args) return std::unexpected(split_args.error());
    auto split_result = apply_split_arguments(cfg, std::move(*split_args));
    if (!split_result) return std::unexpected(split_result.error());
  }

  if (!cfg.env_file.empty()) {
    auto file_vars = read_env_file(cfg.env_file);
    if (!file_vars) {
      cfg.env_file_error = std::move(file_vars.error());
    } else {
      cfg.file_assignments = std::move(*file_vars);
    }
  }
  bool in_command = !cfg.command.empty();
  for (auto p : ctx.positionals) {
    if (in_command) {
      cfg.command.emplace_back(p);
      continue;
    }

    if (p == "-") {
      cfg.ignore_environment = true;
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

auto build_command_line(const SmallVector<std::string, 32>& command,
                        const std::optional<std::string>& argv0_override =
                            std::nullopt) -> std::wstring {
  auto argv0 = argv0_override.has_value() ? utf8_to_wstring(*argv0_override)
                                          : utf8_to_wstring(command.front());
  std::wstring out = quote_windows_command_arg(argv0);
  for (size_t i = 1; i < command.size(); ++i) {
    append_windows_command_arg(out, utf8_to_wstring(command[i]));
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

// [GNU] -v/--debug traces each processing step on stderr in GNU's format:
// "cleaning environ", "unset:    NAME" (for every -u operand), then
// "setenv:   NAME=VALUE" for each assignment (env.c devmsg, uutils #14171).
auto materialize_environment(const Config& cfg)
    -> std::map<std::string, std::string> {
  std::map<std::string, std::string> vars;
  if (!cfg.ignore_environment) {
    vars = parse_env_block();
  } else if (cfg.debug) {
    safeErrorPrint("cleaning environ\n");
  }

  for (const auto& [k, v] : cfg.file_assignments) {
    if (cfg.debug) {
      safeErrorPrint("setenv:   " + k + "=" + v + "\n");
    }
    vars[k] = v;
  }

  for (const auto& unset_name : cfg.unset_names) {
    if (cfg.debug) {
      safeErrorPrint("unset:    " + unset_name + "\n");
    }
    auto it = vars.find(unset_name);
    if (it != vars.end()) {
      vars.erase(it);
    }
  }

  for (const auto& [k, v] : cfg.assignments) {
    if (cfg.debug) {
      safeErrorPrint("setenv:   " + k + "=" + v + "\n");
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
  Win32ErrorTextOptions options;
  options.bad_exe_format_as_permission = true;
  return win32_posix_error_text(error, options);
}

auto run_command(const Config& cfg,
                 const std::map<std::string, std::string>& vars) -> int {
  if (cfg.debug) {
    // [GNU] trace order: chdir, argv0, then executing + quoted args
    // (env.c:902-923, uutils #14171).
    if (!cfg.chdir.empty()) {
      safeErrorPrint("chdir:    '" + cfg.chdir + "'\n");
    }
    if (!cfg.argv0.empty()) {
      safeErrorPrint("argv0:     '" + cfg.argv0 + "'\n");
    }
    safeErrorPrint("executing: " + cfg.command.front() + "\n");
    auto arg0 = cfg.argv0.empty() ? cfg.command.front() : cfg.argv0;
    safeErrorPrint("   arg[0]= '" + arg0 + "'\n");
    for (size_t i = 1; i < cfg.command.size(); ++i) {
      safeErrorPrint("   arg[" + std::to_string(i) + "]= '" + cfg.command[i] +
                     "'\n");
    }
  }

  auto lookup = resolve_executable(cfg.command.front(), vars, cfg.chdir);
  if (lookup.status != ExecutableLookupStatus::Found) {
    const DWORD error = lookup.status == ExecutableLookupStatus::NotExecutable
                            ? ERROR_ACCESS_DENIED
                        : lookup.status == ExecutableLookupStatus::NotADirectory
                            ? ERROR_DIRECTORY
                            : ERROR_FILE_NOT_FOUND;
    safeErrorPrint("env: failed to run command '" + cfg.command.front() +
                   "': " + env_windows_error_text(error) + "\n");
    return env_command_status_from_create_error(error);
  }
  std::optional<std::wstring> application_name = std::move(lookup.path);

  auto cmd_line = build_command_line(
      cfg.command,
      cfg.argv0.empty() ? std::nullopt : std::optional<std::string>(cfg.argv0));
  auto env_block = build_environment_block(vars);
  std::wstring working_directory;
  LPCWSTR working_directory_arg =
      make_working_directory_arg(cfg.chdir, working_directory);
  if (!cfg.chdir.empty() && working_directory_arg == nullptr) {
    cp::report_custom_error(L"env", L"cannot change directory");
    return 125;
  }

  STARTUPINFOW si{sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(
      application_name.has_value() ? application_name->c_str() : nullptr,
      cmd_line.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT,
      env_block.data(), working_directory_arg, &si, &pi);
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

  auto vars = materialize_environment(cfg);

  if (!cfg.command.empty()) {
    return run_command(cfg, vars);
  }

  print_env(vars, cfg.null_terminated);
  return 0;
}

}  // namespace env_pipeline

REGISTER_COMMAND(
    env, "env", "env [OPTION]... [NAME=VALUE]... [COMMAND [ARG]...]",
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
    "printenv(1), which(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    ENV_OPTIONS) {
  using namespace env_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"env");
    return 125;
  }
  return run(*cfg);
}
