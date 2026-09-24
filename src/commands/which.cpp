/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implemention for which.
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

// [GNU] -a, --all: print all matching pathnames of each argument
// [GNU] -s, --silent, --quiet: suppress all normal output
// [GNU] --skip-dot: skip directories in PATH that start with a dot
// [GNU] --skip-tilde: skip directories in PATH that start with a tilde
// [GNU] --show-dot: print ./COMMAND if directory starts with dot
// [GNU] --show-tilde: output a tilde for the HOME directory
// [GNU] --tty-only: only show results on a terminal
// [GNU] --read-alias: read list of aliases from stdin
// [GNU] --skip-alias: do not read aliases
// [GNU] --read-functions: read shell functions from stdin
// [GNU] --skip-functions: do not read shell functions
auto constexpr WHICH_OPTIONS = std::array{
    OPTION("-a", "--all", "print all matching pathnames of each argument"),
    // [GNU] -i: read aliases from stdin (same as --read-alias); hidden
    OPTION("-i", "", "", BOOL_TYPE),
    // [GNU] -v: print version (declared so --version interception applies)
    OPTION("-v", "--version", "print version information"),
    OPTION("-s", "--silent", "suppress all normal output"),
    OPTION("", "--quiet", "suppress all normal output"),
    OPTION("", "--skip-dot", "skip directories in PATH that start with a dot"),
    OPTION("", "--skip-tilde",
           "skip directories in PATH that start with a tilde and HOME matches"),
    OPTION("", "--show-dot",
           "if a directory in PATH starts with a dot, print ./COMMAND"),
    OPTION("", "--show-tilde", "output a tilde for the HOME directory"),
    OPTION("", "--tty-only", "only show results on a terminal"),
    OPTION("", "--read-alias", "read list of aliases from stdin"),
    OPTION("", "--skip-alias", "do not read aliases"),
    OPTION("", "--read-functions", "read shell functions from stdin"),
    OPTION("", "--skip-functions", "do not read shell functions")};

namespace which_pipeline {
namespace cp = core::pipeline;
namespace fs = std::filesystem;

struct Config {
  bool all = false;
  bool silent = false;
  bool skip_dot = false;
  bool skip_tilde = false;
  bool show_dot = false;
  bool show_tilde = false;
  bool tty_only = false;
  bool read_alias = false;
  bool skip_alias = false;
  bool read_functions = false;
  bool skip_functions = false;
  fs::path cwd;
  std::optional<fs::path> home;
  SmallVector<std::string, 32> names;
};

struct SearchDir {
  fs::path dir;
  bool starts_with_dot = false;
  bool starts_with_tilde = false;
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

auto normalize_win_shell_path(std::string_view text) -> std::string {
  auto make_drive_path = [](char drive, std::string_view rest) {
    std::string out;
    out.reserve(rest.size() + 2);
    out.push_back(
        static_cast<char>(std::toupper(static_cast<unsigned char>(drive))));
    out.push_back(':');
    out.append(rest.data(), rest.size());
    return out;
  };

  if (text.size() >= 3 && (text[0] == '/' || text[0] == '\\') &&
      std::isalpha(static_cast<unsigned char>(text[1])) &&
      (text[2] == '/' || text[2] == '\\')) {
    return make_drive_path(text[1], text.substr(2));
  }

  constexpr std::string_view cygdrive = "/cygdrive/";
  if (text.size() > cygdrive.size() + 1 && text.starts_with(cygdrive) &&
      std::isalpha(static_cast<unsigned char>(text[cygdrive.size()])) &&
      (text[cygdrive.size() + 1] == '/' || text[cygdrive.size() + 1] == '\\')) {
    return make_drive_path(text[cygdrive.size()],
                           text.substr(cygdrive.size() + 1));
  }

  return std::string(text);
}

auto path_from_utf8(std::string_view text) -> fs::path {
  return fs::path(utf8_to_wstring(normalize_win_shell_path(text)));
}

auto path_to_utf8(const fs::path& p) -> std::string {
  return wstring_to_utf8(p.generic_wstring());
}

auto ascii_lower(std::string text) -> std::string {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

auto ensure_trailing_slash(std::string text) -> std::string {
  if (!text.empty() && text.back() != '/') text.push_back('/');
  return text;
}

auto starts_with_tilde(std::string_view text) -> bool {
  return !text.empty() && text.front() == '~';
}

auto starts_with_dot(std::string_view text) -> bool {
  return !text.empty() && text.front() == '.';
}

auto make_absolute_normalized(const fs::path& p) -> fs::path {
  std::error_code ec;
  fs::path absolute = p.is_absolute() ? p : fs::absolute(p, ec);
  if (ec) absolute = p;
  return absolute.lexically_normal();
}

auto get_current_directory() -> fs::path {
  std::error_code ec;
  auto cwd = fs::current_path(ec);
  if (ec) return fs::path(L".");
  return cwd.lexically_normal();
}

auto get_home_directory() -> std::optional<fs::path> {
  if (auto home = get_env_utf8(L"HOME"); home.has_value() && !home->empty()) {
    return make_absolute_normalized(path_from_utf8(*home));
  }
  if (auto profile = get_env_utf8(L"USERPROFILE");
      profile.has_value() && !profile->empty()) {
    return make_absolute_normalized(path_from_utf8(*profile));
  }

  auto drive = get_env_utf8(L"HOMEDRIVE");
  auto path = get_env_utf8(L"HOMEPATH");
  if (drive.has_value() && path.has_value() && !drive->empty() &&
      !path->empty()) {
    return make_absolute_normalized(path_from_utf8(*drive + *path));
  }
  return std::nullopt;
}

auto expand_leading_tilde(std::string_view text,
                          const std::optional<fs::path>& home) -> std::string {
  if (!starts_with_tilde(text) || !home.has_value()) return std::string(text);

  if (text.size() == 1) return path_to_utf8(*home);
  if (text[1] == '/' || text[1] == '\\') {
    auto base = path_to_utf8(*home);
    return ensure_trailing_slash(base) + std::string(text.substr(2));
  }

  // GNU which delegates full ~user handling to tilde expansion. Windows has no
  // equivalent user database here, so keep such forms literal.
  return std::string(text);
}

auto get_path_entries() -> std::vector<std::string> {
  std::vector<std::string> entries;

  // GNU which on Windows searches the current directory before PATH because
  // that is what CreateProcess-style command lookup does.
  entries.emplace_back(".");

  auto path_env = get_env_utf8(L"PATH");
  if (!path_env.has_value()) return entries;

  for (auto& entry : split_semicolon(*path_env)) {
    entries.push_back(entry.empty() ? std::string(".") : std::move(entry));
  }
  return entries;
}

auto get_pathext_entries() -> std::vector<std::string> {
  auto ext_env = get_env_utf8(L"PATHEXT");
  std::vector<std::string> exts;
  auto append_unique = [&exts](std::string ext) {
    if (ext.empty()) return;
    if (ext.front() != '.') ext.insert(ext.begin(), '.');
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    if (std::find(exts.begin(), exts.end(), ext) == exts.end()) {
      exts.push_back(std::move(ext));
    }
  };

  if (!ext_env.has_value() || ext_env->empty()) {
    for (auto ext : {".exe", ".ps1", ".bat", ".cmd", ".com"}) {
      append_unique(ext);
    }
  } else {
    for (auto& ext : split_semicolon(*ext_env)) {
      append_unique(std::move(ext));
    }
    if (exts.empty()) {
      for (auto ext : {".exe", ".ps1", ".bat", ".cmd", ".com"}) {
        append_unique(ext);
      }
    }
  }

  // `which` describes what winuxsh/winuxcmd can launch, not only what cmd.exe
  // has in the user's PATHEXT. Keep supported Windows wrappers ahead of
  // extensionless POSIX scripts even if PATHEXT is minimal or unusual.
  for (auto ext : {".exe", ".com", ".bat", ".cmd", ".ps1"}) {
    append_unique(ext);
  }
  return exts;
}

auto has_path_separator(std::string_view s) -> bool {
  return s.find('/') != std::string_view::npos ||
         s.find('\\') != std::string_view::npos;
}

auto exists_command_candidate(const fs::path& p) -> bool {
  DWORD attrs = GetFileAttributesW(p.wstring().c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

auto with_extensions(const fs::path& base, const std::vector<std::string>& exts)
    -> std::vector<fs::path> {
  std::vector<fs::path> out;
  if (base.has_extension()) {
    out.push_back(base);
    return out;
  }

  for (const auto& ext : exts) {
    fs::path p = base;
    p += utf8_to_wstring(ext);
    out.push_back(std::move(p));
  }
  out.push_back(base);
  return out;
}

auto make_search_dir(std::string entry, const Config& cfg)
    -> std::optional<SearchDir> {
  if (entry.empty()) entry = ".";

  const bool tilde = starts_with_tilde(entry);
  if (cfg.skip_tilde && tilde) return std::nullopt;

  auto expanded = expand_leading_tilde(entry, cfg.home);
  fs::path dir = path_from_utf8(expanded);
  if (cfg.skip_dot && !dir.is_absolute()) return std::nullopt;

  return SearchDir{.dir = std::move(dir),
                   .starts_with_dot = starts_with_dot(expanded),
                   .starts_with_tilde = tilde};
}

auto command_operand_search_dirs(std::string_view operand, const Config& cfg)
    -> std::vector<SearchDir> {
  std::vector<SearchDir> dirs;

  if (!has_path_separator(operand)) {
    for (auto& entry : get_path_entries()) {
      if (auto dir = make_search_dir(std::move(entry), cfg); dir.has_value()) {
        dirs.push_back(std::move(*dir));
      }
    }
    return dirs;
  }

  std::string operand_str(operand);
  fs::path operand_path = path_from_utf8(operand_str);
  auto parent = operand_path.parent_path();
  std::string parent_text;

  if (operand_path.is_absolute() || starts_with_tilde(operand_str) ||
      starts_with_dot(operand_str)) {
    parent_text = parent.empty() ? std::string(".") : path_to_utf8(parent);
  } else {
    auto rel_parent = parent.empty() ? std::string(".") : path_to_utf8(parent);
    parent_text = ensure_trailing_slash(".") + rel_parent;
  }

  if (auto dir = make_search_dir(parent_text, cfg); dir.has_value()) {
    dirs.push_back(std::move(*dir));
  }
  return dirs;
}

auto command_operand_basename(std::string_view operand) -> std::string {
  if (!has_path_separator(operand)) return std::string(operand);
  return path_to_utf8(path_from_utf8(operand).filename());
}

auto path_suffix_under(const fs::path& path, const fs::path& base)
    -> std::optional<std::string> {
  auto path_text = path_to_utf8(make_absolute_normalized(path));
  auto base_text =
      ensure_trailing_slash(path_to_utf8(make_absolute_normalized(base)));
  auto path_key = ascii_lower(path_text);
  auto base_key = ascii_lower(base_text);

  if (!path_key.starts_with(base_key)) return std::nullopt;
  return path_text.substr(base_text.size());
}

auto display_hit(const fs::path& candidate, const SearchDir& dir,
                 const Config& cfg) -> std::optional<std::string> {
  auto normalized = make_absolute_normalized(candidate);

  if (cfg.skip_tilde && cfg.home.has_value() &&
      path_suffix_under(normalized, *cfg.home).has_value()) {
    return std::nullopt;
  }

  if (cfg.show_dot && dir.starts_with_dot) {
    if (auto suffix = path_suffix_under(normalized, cfg.cwd);
        suffix.has_value() && !suffix->empty()) {
      return std::string("./") + *suffix;
    }
  }

  if (cfg.show_tilde && cfg.home.has_value()) {
    if (auto suffix = path_suffix_under(normalized, *cfg.home);
        suffix.has_value() && !suffix->empty()) {
      return std::string("~/") + *suffix;
    }
  }

  return path_to_utf8(normalized);
}

auto find_one(std::string_view name, const Config& cfg)
    -> std::vector<std::string> {
  SmallVector<std::string, 64> hits;
  std::unordered_set<std::string> seen;
  const auto pathext = get_pathext_entries();
  auto basename = command_operand_basename(name);

  for (const auto& dir : command_operand_search_dirs(name, cfg)) {
    fs::path base = dir.dir / path_from_utf8(basename);
    for (const auto& candidate : with_extensions(base, pathext)) {
      if (!exists_command_candidate(candidate)) continue;

      auto display = display_hit(candidate, dir, cfg);
      if (!display.has_value()) continue;

      if (seen.insert(*display).second) {
        hits.push_back(std::move(*display));
      }
      if (!cfg.all) return std::vector<std::string>(hits.begin(), hits.end());
    }
  }

  return std::vector<std::string>(hits.begin(), hits.end());
}

auto build_config(const CommandContext<WHICH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  cfg.silent = ctx.get<bool>("--silent", false) || ctx.get<bool>("-s", false) ||
               ctx.get<bool>("--quiet", false);  // [DIFFERS]
  cfg.skip_dot = ctx.get<bool>("--skip-dot", false);
  cfg.skip_tilde = ctx.get<bool>("--skip-tilde", false);
  cfg.show_dot = ctx.get<bool>("--show-dot", false);
  cfg.show_tilde = ctx.get<bool>("--show-tilde", false);
  cfg.tty_only = ctx.get<bool>("--tty-only", false);  // [DIFFERS]
  // [GNU] -i is the short form of --read-alias (#1071).
  cfg.read_alias =
      ctx.get<bool>("--read-alias", false) || ctx.get<bool>("-i", false);
  cfg.skip_alias = ctx.get<bool>("--skip-alias", false);          // [DIFFERS]
  cfg.read_functions = ctx.get<bool>("--read-functions", false);  // [DIFFERS]
  cfg.skip_functions = ctx.get<bool>("--skip-functions", false);  // [DIFFERS]
  cfg.cwd = make_absolute_normalized(get_current_directory());
  cfg.home = get_home_directory();

  for (auto arg : ctx.positionals) cfg.names.emplace_back(arg);
  if (cfg.names.empty()) return std::unexpected("missing command operand");
  return cfg;
}

auto run(const Config& cfg) -> int {
  // [DIFFERS] --tty-only: suppress output when stdout is not a terminal
  if (cfg.tty_only && !_isatty(_fileno(stdout))) {
    // Still compute exit code but suppress all output
    bool all_found = true;
    for (const auto& name : cfg.names) {
      if (find_one(name, cfg).empty()) {
        all_found = false;
      }
    }
    return all_found ? 0 : 1;
  }

  bool all_found = true;
  for (const auto& name : cfg.names) {
    auto hits = find_one(name, cfg);
    if (hits.empty()) {
      all_found = false;
      continue;
    }
    // [DIFFERS] --silent: suppress all normal output
    if (!cfg.silent) {
      for (const auto& h : hits) {
        safePrint(h);
        safePrint("\n");
      }
    }
  }
  return all_found ? 0 : 1;
}

}  // namespace which_pipeline

REGISTER_COMMAND(which, "which", "which [OPTION]... COMMAND...",
                 "Locate COMMAND in PATH.",
                 "  which ls\n"
                 "  which -a python",
                 "where(1), command(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", WHICH_OPTIONS) {
  using namespace which_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"which");
    return 1;
  }

  // [DIFFERS] --read-alias and --read-functions are not supported on Windows
  if (cfg->read_alias) {
    safeErrorPrintLn("which: --read-alias is not supported on Windows");
    return 1;
  }
  if (cfg->read_functions) {
    safeErrorPrintLn("which: --read-functions is not supported on Windows");
    return 1;
  }

  return run(*cfg);
}
