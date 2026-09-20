// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for cygpath command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr CYGPATH_OPTIONS = std::array{
    // [EXT]
    OPTION("-u", "--unix", "print Unix form of NAME"),
    // [EXT]
    OPTION("-w", "--windows", "print Windows form of NAME"),
    // [EXT]
    OPTION("-m", "--mixed", "print Windows form, with regular slashes"),
    // [EXT]
    OPTION("-p", "--path", "NAME is a PATH list"),
    // [EXT]
    OPTION("-t", "--type", "print TYPE form: dos, mixed, unix, or windows",
           STRING_TYPE),
    // [EXT]
    OPTION("-d", "--dos", "print DOS short form of NAMEs"),
    // [EXT]
    OPTION("-a", "--absolute",
           "output absolute path [accepted, partial lexical support]"),
    // [EXT]
    OPTION("-i", "--ignore", "ignore missing or empty arguments"),
    // [EXT]
    OPTION("-f", "--file", "read input paths from FILE", STRING_TYPE),
    // [EXT]
    OPTION("-o", "--option",
           "read options from FILE as well [accepted, not implemented]"),
    // [EXT]
    OPTION("-l", "--long-name", "print Windows long form"),
    // [EXT]
    OPTION("-s", "--short-name", "print DOS short form"),
    // [EXT]
    OPTION("-U", "--proc-cygdrive", "emit /proc/cygdrive paths"),
    // [EXT]
    OPTION("-C", "--codepage", "print Windows path in codepage CP",
           STRING_TYPE),
    // [EXT]
    OPTION("-M", "--mode", "report file text/binary mode"),
    // [EXT]
    OPTION("-A", "--allusers", "use All Users for special folders"),
    // [EXT]
    OPTION("-D", "--desktop", "output Desktop directory"),
    // [EXT]
    OPTION("-H", "--homeroot", "output Profiles directory"),
    // [EXT]
    OPTION("-O", "--mydocs", "output My Documents directory"),
    // [EXT]
    OPTION("-P", "--smprograms", "output Start Menu Programs directory"),
    // [EXT]
    OPTION("-S", "--sysdir", "output system directory"),
    // [EXT]
    OPTION("-W", "--windir", "output Windows directory"),
    // [EXT]
    OPTION("-F", "--folder", "output special folder ID [not implemented]",
           STRING_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
enum class OutputMode { Unix, Windows, Mixed };

auto ascii_lower(char ch) -> char {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

auto ascii_upper(char ch) -> char {
  return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

auto is_drive_letter(char ch) -> bool {
  return std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

auto has_windows_drive(std::string_view path) -> bool {
  return path.size() >= 2 && is_drive_letter(path[0]) && path[1] == ':';
}

auto replace_char(std::string value, char from, char to) -> std::string {
  std::ranges::replace(value, from, to);
  return value;
}

auto split_keep_empty(std::string_view value, char separator)
    -> std::vector<std::string> {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= value.size()) {
    size_t pos = value.find(separator, start);
    if (pos == std::string_view::npos) {
      parts.emplace_back(value.substr(start));
      break;
    }
    parts.emplace_back(value.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

auto join_parts(const std::vector<std::string>& parts, char separator)
    -> std::string {
  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) result.push_back(separator);
    result += parts[i];
  }
  return result;
}

auto remove_extended_prefix(std::string path) -> std::string {
  if (path.starts_with(R"(\\?\UNC\)")) {
    return R"(\\)" + path.substr(8);
  }
  if (path.starts_with(R"(\\?\)")) {
    return path.substr(4);
  }
  return path;
}

auto make_absolute_for_windows_input(std::string path) -> std::string {
  if (path.empty() || has_windows_drive(path) || path.starts_with("\\\\") ||
      path.starts_with("//")) {
    return path;
  }

  std::filesystem::path fs_path(utf8_to_wstring(path));
  std::error_code ec;
  auto absolute = std::filesystem::absolute(fs_path, ec);
  if (ec) return path;
  return wstring_to_utf8(absolute.wstring());
}

auto make_absolute_for_posix_input(std::string path) -> std::string {
  if (path.empty() || path.starts_with('/')) return path;
  return make_absolute_for_windows_input(std::move(path));
}

// Cygwin's source delegates conversion to cygwin_conv_path.  WinuxCmd does not
// link against the Cygwin runtime, so this mirrors the hot MSYS/Cygwin shapes:
// C:\x -> /c/x, /c/x -> C:\x, and UNC slashes are preserved.
auto env_var(std::wstring_view name) -> std::string {
  DWORD needed =
      GetEnvironmentVariableW(std::wstring(name).c_str(), nullptr, 0);
  if (needed == 0) return {};
  std::wstring value(needed, L'\0');
  DWORD written =
      GetEnvironmentVariableW(std::wstring(name).c_str(), value.data(), needed);
  if (written == 0) return {};
  value.resize(written);
  return wstring_to_utf8(value);
}

auto windows_directory(bool system_directory) -> std::string {
  std::wstring buffer(MAX_PATH, L'\0');
  UINT written = system_directory
                     ? GetSystemDirectoryW(buffer.data(), MAX_PATH)
                     : GetWindowsDirectoryW(buffer.data(), MAX_PATH);
  if (written == 0) return {};
  if (written >= MAX_PATH) {
    buffer.resize(written + 1, L'\0');
    written = system_directory
                  ? GetSystemDirectoryW(buffer.data(), written + 1)
                  : GetWindowsDirectoryW(buffer.data(), written + 1);
  }
  buffer.resize(written);
  return wstring_to_utf8(buffer);
}

auto parent_path_string(std::string path) -> std::string {
  std::filesystem::path p(utf8_to_wstring(path));
  return wstring_to_utf8(p.parent_path().wstring());
}

auto append_path(std::string base, std::string_view tail) -> std::string {
  if (base.empty()) return {};
  std::filesystem::path p(utf8_to_wstring(base));
  p /= utf8_to_wstring(std::string(tail));
  return wstring_to_utf8(p.wstring());
}

auto windows_to_unix(std::string path, bool proc_cygdrive) -> std::string {
  path = remove_extended_prefix(std::move(path));
  path = replace_char(std::move(path), '\\', '/');

  if (has_windows_drive(path)) {
    char drive = ascii_lower(path[0]);
    std::string tail = path.substr(2);
    if (!tail.empty() && tail.front() != '/') tail.insert(tail.begin(), '/');
    if (proc_cygdrive) {
      return "/proc/cygdrive/" + std::string(1, drive) + tail;
    }
    return "/" + std::string(1, drive) + tail;
  }

  return path;
}

auto unix_to_windows(std::string path, bool mixed) -> std::string {
  if (path.starts_with("/cygdrive/") && path.size() >= 11 &&
      is_drive_letter(path[10]) && (path.size() == 11 || path[11] == '/')) {
    std::string tail = path.substr(11);
    if (!tail.empty() && tail.front() != '/') tail.insert(tail.begin(), '/');
    path = std::string(1, ascii_upper(path[10])) + ":" + tail;
  } else if (path.size() >= 2 && path[0] == '/' && is_drive_letter(path[1]) &&
             (path.size() == 2 || path[2] == '/')) {
    std::string tail = path.substr(2);
    if (!tail.empty() && tail.front() != '/') tail.insert(tail.begin(), '/');
    path = std::string(1, ascii_upper(path[1])) + ":" + tail;
  }

  return mixed ? replace_char(std::move(path), '\\', '/')
               : replace_char(std::move(path), '/', '\\');
}

auto short_path_name(std::string path) -> std::string {
  std::wstring wpath = utf8_to_wstring(path);
  DWORD needed = GetShortPathNameW(wpath.c_str(), nullptr, 0);
  if (needed == 0) return path;
  std::wstring out(needed, L'\0');
  DWORD written = GetShortPathNameW(wpath.c_str(), out.data(), needed);
  if (written == 0) return path;
  out.resize(written);
  return wstring_to_utf8(out);
}

auto long_path_name(std::string path) -> std::string {
  std::wstring wpath = utf8_to_wstring(path);
  DWORD needed = GetLongPathNameW(wpath.c_str(), nullptr, 0);
  if (needed == 0) return path;
  std::wstring out(needed, L'\0');
  DWORD written = GetLongPathNameW(wpath.c_str(), out.data(), needed);
  if (written == 0) return path;
  out.resize(written);
  return wstring_to_utf8(out);
}

auto convert_one(std::string path, OutputMode mode, bool absolute,
                 bool proc_cygdrive, bool short_name, bool long_name)
    -> std::string {
  if (absolute) {
    path = (mode == OutputMode::Unix)
               ? make_absolute_for_windows_input(std::move(path))
               : make_absolute_for_posix_input(std::move(path));
  }

  if (mode != OutputMode::Unix) {
    path = unix_to_windows(std::move(path), false);
    if (short_name) path = short_path_name(std::move(path));
    if (long_name) path = long_path_name(std::move(path));
    return mode == OutputMode::Mixed ? replace_char(std::move(path), '\\', '/')
                                     : replace_char(std::move(path), '/', '\\');
  }

  if (short_name) path = short_path_name(std::move(path));
  if (long_name) path = long_path_name(std::move(path));
  switch (mode) {
    case OutputMode::Unix:
      return windows_to_unix(std::move(path), proc_cygdrive);
    case OutputMode::Windows:
      return unix_to_windows(std::move(path), false);
    case OutputMode::Mixed:
      return unix_to_windows(std::move(path), true);
  }

  std::unreachable();
}

auto convert_path_list(std::string_view value, OutputMode mode, bool absolute,
                       bool proc_cygdrive, bool short_name, bool long_name)
    -> std::string {
  char input_separator = mode == OutputMode::Unix ? ';' : ':';
  char output_separator = mode == OutputMode::Unix ? ':' : ';';
  auto parts = split_keep_empty(value, input_separator);
  for (auto& part : parts) {
    if (!part.empty()) {
      part = convert_one(std::move(part), mode, absolute, proc_cygdrive,
                         short_name, long_name);
    }
  }
  return join_parts(parts, output_separator);
}

auto unsupported_options(const CommandContext<CYGPATH_OPTIONS.size()>& ctx)
    -> std::vector<std::string_view> {
  std::vector<std::string_view> names;
  for (const auto& occurrence : ctx.options.occurrences()) {
    const auto& meta = (*ctx.metas)[occurrence.index];
    auto short_name = meta.short_name;
    auto long_name = meta.long_name;

    if (short_name == "-o" || short_name == "-F") {
      names.push_back(!long_name.empty() ? long_name : short_name);
    }
  }
  return names;
}
}  // namespace

// ======================================================
// Pipeline components
// ======================================================
namespace cygpath_pipeline {
namespace cp = core::pipeline;

struct Config {
  OutputMode mode = OutputMode::Unix;
  bool absolute = false;
  bool ignore = false;
  bool is_path = false;
  bool proc_cygdrive = false;
  bool short_name = false;
  bool long_name = false;
  bool report_mode = false;
  std::vector<std::string> paths;
};

auto read_paths_file(std::string_view file)
    -> cp::Result<std::vector<std::string>> {
  std::string data;
  if (file == "-") {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    data = buffer.str();
  } else {
    std::ifstream in{std::filesystem::path(utf8_to_wstring(std::string(file)))};
    if (!in.is_open()) {
      return std::unexpected("cannot open '" + std::string(file) +
                             "' for reading");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    data = buffer.str();
  }

  std::vector<std::string> paths;
  size_t start = 0;
  while (start < data.size()) {
    size_t end = data.find('\n', start);
    if (end == std::string::npos) end = data.size();
    std::string line = data.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    paths.push_back(std::move(line));
    if (end == data.size()) break;
    start = end + 1;
  }
  return paths;
}

auto selected_special_paths(const CommandContext<CYGPATH_OPTIONS.size()>& ctx)
    -> std::vector<std::string> {
  std::vector<std::string> paths;
  const bool all_users =
      ctx.get<bool>("-A", false) || ctx.get<bool>("--allusers", false);
  const auto userprofile = env_var(L"USERPROFILE");
  const auto public_dir = env_var(L"PUBLIC");
  const auto appdata = env_var(L"APPDATA");
  const auto programdata = env_var(L"ProgramData");

  if (ctx.get<bool>("-D", false) || ctx.get<bool>("--desktop", false)) {
    paths.push_back(all_users ? append_path(public_dir, "Desktop")
                              : append_path(userprofile, "Desktop"));
  }
  if (ctx.get<bool>("-H", false) || ctx.get<bool>("--homeroot", false)) {
    paths.push_back(parent_path_string(userprofile));
  }
  if (ctx.get<bool>("-O", false) || ctx.get<bool>("--mydocs", false)) {
    paths.push_back(append_path(userprofile, "Documents"));
  }
  if (ctx.get<bool>("-P", false) || ctx.get<bool>("--smprograms", false)) {
    paths.push_back(
        all_users
            ? append_path(programdata,
                          "Microsoft\\Windows\\Start Menu\\Programs")
            : append_path(appdata, "Microsoft\\Windows\\Start Menu\\Programs"));
  }
  if (ctx.get<bool>("-S", false) || ctx.get<bool>("--sysdir", false)) {
    paths.push_back(windows_directory(true));
  }
  if (ctx.get<bool>("-W", false) || ctx.get<bool>("--windir", false)) {
    paths.push_back(windows_directory(false));
  }

  std::erase_if(paths, [](const std::string& value) { return value.empty(); });
  return paths;
}

auto build_config(const CommandContext<CYGPATH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto unsupported = unsupported_options(ctx);
  if (!unsupported.empty()) {
    return std::unexpected("option '" + std::string(unsupported.front()) +
                           "' is accepted for compatibility but not "
                           "implemented yet");
  }

  size_t mode_count = 0;
  if (ctx.get<bool>("-u", false) || ctx.get<bool>("--unix", false)) {
    cfg.mode = OutputMode::Unix;
    ++mode_count;
  }
  if (ctx.get<bool>("-w", false) || ctx.get<bool>("--windows", false)) {
    cfg.mode = OutputMode::Windows;
    ++mode_count;
  }
  if (ctx.get<bool>("-m", false) || ctx.get<bool>("--mixed", false)) {
    cfg.mode = OutputMode::Mixed;
    ++mode_count;
  }

  std::string type = ctx.get<std::string>("-t", "");
  if (type.empty()) type = ctx.get<std::string>("--type", "");
  if (!type.empty()) {
    std::ranges::transform(type, type.begin(), ascii_lower);
    if (type == "unix") {
      cfg.mode = OutputMode::Unix;
    } else if (type == "windows") {
      cfg.mode = OutputMode::Windows;
    } else if (type == "mixed") {
      cfg.mode = OutputMode::Mixed;
    } else if (type == "dos") {
      cfg.mode = OutputMode::Windows;
      cfg.short_name = true;
    } else {
      return std::unexpected("invalid type '" + type +
                             "'; expected dos, mixed, unix, or windows");
    }
    ++mode_count;
  }

  if (mode_count > 1) {
    return std::unexpected(
        "only one output type may be specified (-u, -w, -m, or -t)");
  }

  cfg.absolute =
      ctx.get<bool>("-a", false) || ctx.get<bool>("--absolute", false);
  cfg.ignore = ctx.get<bool>("-i", false) || ctx.get<bool>("--ignore", false);
  cfg.is_path = ctx.get<bool>("-p", false) || ctx.get<bool>("--path", false);
  cfg.proc_cygdrive =
      ctx.get<bool>("-U", false) || ctx.get<bool>("--proc-cygdrive", false);
  cfg.short_name = cfg.short_name || ctx.get<bool>("-d", false) ||
                   ctx.get<bool>("--dos", false) ||
                   ctx.get<bool>("-s", false) ||
                   ctx.get<bool>("--short-name", false);
  if (ctx.get<bool>("-d", false) || ctx.get<bool>("--dos", false)) {
    cfg.mode = OutputMode::Windows;
  }
  cfg.long_name =
      ctx.get<bool>("-l", false) || ctx.get<bool>("--long-name", false);
  cfg.report_mode =
      ctx.get<bool>("-M", false) || ctx.get<bool>("--mode", false);

  (void)ctx.get<std::string>("-C", "");
  (void)ctx.get<std::string>("--codepage", "");

  cfg.paths = selected_special_paths(ctx);

  std::string path_file = ctx.get<std::string>("-f", "");
  if (path_file.empty()) path_file = ctx.get<std::string>("--file", "");
  if (!path_file.empty()) {
    auto file_paths = read_paths_file(path_file);
    if (!file_paths) return std::unexpected(file_paths.error());
    cfg.paths.insert(cfg.paths.end(), file_paths->begin(), file_paths->end());
  }

  if (ctx.positionals.empty() && cfg.paths.empty()) {
    if (cfg.ignore) return cfg;
    return std::unexpected("missing operand");
  }

  for (const auto& path : ctx.positionals) {
    cfg.paths.push_back(std::string(path));
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  for (const auto& path : cfg.paths) {
    if (path.empty()) {
      if (cfg.ignore) continue;
      safeErrorPrintLn("cygpath: can't convert empty path");
      return 1;
    }

    if (cfg.report_mode) {
      safePrintLn("binmode");
      continue;
    }

    auto result =
        cfg.is_path
            ? convert_path_list(path, cfg.mode, cfg.absolute, cfg.proc_cygdrive,
                                cfg.short_name, cfg.long_name)
            : convert_one(path, cfg.mode, cfg.absolute, cfg.proc_cygdrive,
                          cfg.short_name, cfg.long_name);
    safePrintLn(result);
  }

  return 0;
}

}  // namespace cygpath_pipeline

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(cygpath,
                 /* name */
                 "cygpath",

                 /* synopsis */
                 "cygpath [OPTION] NAME...",

                 /* description */
                 "Convert path names between Windows and Unix formats.\n"
                 "Convert path names between Windows and Unix/Cygwin formats.\n"
                 "Default is to convert Windows paths to Unix paths.",

                 /* examples */
                 "  cygpath -u C:\\Users\\John\\Documents\n"
                 "  cygpath -w /home/user/file.txt\n"
                 "  cygpath -m /c/Users/John/Documents",

                 /* see_also */
                 "realpath(1), pwd(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 CYGPATH_OPTIONS) {
  using namespace cygpath_pipeline;
  using namespace core::pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("cygpath: ");
    safeErrorPrintLn(cfg_result.error());
    return 1;
  }

  return run(*cfg_result);
}
