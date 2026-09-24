// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for mktemp.
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

auto constexpr MKTEMP_OPTIONS = std::array{
    // [GNU]
    OPTION("-d", "--directory", "make a directory instead of a file",
           BOOL_TYPE),
    // [GNU]
    OPTION("-u", "--dry-run", "do not actually create anything", BOOL_TYPE),
    // [GNU]
    OPTION("-q", "--quiet", "suppress diagnostics", BOOL_TYPE),
    // [GNU]
    OPTION("", "--suffix",
           "append SUFFIX to TEMPLATE; SUFFIX must not contain a path "
           "separator",
           STRING_TYPE),
    // [GNU]
    OPTION("-t", "",
           "interpret TEMPLATE as a single file name component, relative to "
           "a directory"),
    // [GNU]
    OPTION("-p", "--tmpdir",
           "interpret TEMPLATE relative to DIR; if DIR is not specified, use "
           "$TMPDIR",
           STRING_TYPE)};

namespace mktemp_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool make_directory = false;
  bool dry_run = false;
  bool quiet = false;
  bool use_tmpdir_flag = false;
  std::string tmpdir;
  std::string template_str;
  std::string suffix;
};
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

auto getenv_path(std::wstring_view name) -> std::optional<std::string> {
  std::wstring key(name);
  DWORD needed = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
  if (needed == 0) return std::nullopt;

  std::wstring value(needed, wchar_t{});
  DWORD written = GetEnvironmentVariableW(key.c_str(), value.data(), needed);
  if (written == 0) return std::nullopt;

  value.resize(written);
  if (value.empty()) return std::nullopt;
  return normalize_win_shell_path(wstring_to_utf8(value));
}

auto long_existing_prefix_path(std::filesystem::path path)
    -> std::filesystem::path {
  std::vector<std::filesystem::path> suffix;
  std::error_code ec;
  while (!path.empty() && !std::filesystem::exists(path, ec)) {
    suffix.push_back(path.filename());
    auto parent = path.parent_path();
    if (parent == path) break;
    path = parent;
    ec.clear();
  }

  if (!path.empty()) {
    std::wstring base = path.wstring();
    std::replace(base.begin(), base.end(), L'/', L'\\');
    DWORD full_needed = GetFullPathNameW(base.c_str(), 0, nullptr, nullptr);
    if (full_needed > 0) {
      std::wstring full(full_needed, wchar_t{});
      DWORD full_written =
          GetFullPathNameW(base.c_str(), full_needed, full.data(), nullptr);
      if (full_written > 0 && full_written < full_needed) {
        full.resize(full_written);
        base = std::move(full);
      }
    }
    DWORD needed = GetLongPathNameW(base.c_str(), nullptr, 0);
    if (needed > 0) {
      std::wstring buffer(static_cast<size_t>(needed) + 1, wchar_t{});
      DWORD written = GetLongPathNameW(base.c_str(), buffer.data(),
                                       static_cast<DWORD>(buffer.size()));
      if (written > 0 && written < buffer.size()) {
        buffer.resize(written);
        path = std::filesystem::path(buffer);
      }
    }
  }

  for (auto it = suffix.rbegin(); it != suffix.rend(); ++it) {
    path /= *it;
  }
  return path;
}

auto expand_user_profile_short_name(std::filesystem::path path)
    -> std::filesystem::path {
  DWORD needed = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
  if (needed == 0) return path;

  std::wstring profile(needed, wchar_t{});
  DWORD written =
      GetEnvironmentVariableW(L"USERPROFILE", profile.data(), needed);
  if (written == 0 || written >= needed) return path;
  profile.resize(written);
  if (profile.empty()) return path;

  std::wstring short_profile(MAX_PATH, wchar_t{});
  DWORD short_written =
      GetShortPathNameW(profile.c_str(), short_profile.data(),
                        static_cast<DWORD>(short_profile.size()));
  if (short_written == 0) return path;
  if (short_written >= short_profile.size()) {
    short_profile.assign(static_cast<size_t>(short_written) + 1, wchar_t{});
    short_written = GetShortPathNameW(profile.c_str(), short_profile.data(),
                                      static_cast<DWORD>(short_profile.size()));
    if (short_written == 0 || short_written >= short_profile.size())
      return path;
  }
  short_profile.resize(short_written);

  auto normalize = [](std::wstring text) {
    std::replace(text.begin(), text.end(), L'/', L'\\');
    return text;
  };
  auto lower = [](std::wstring text) {
    std::ranges::transform(text, text.begin(), [](wchar_t ch) {
      return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
  };

  std::wstring actual = normalize(path.wstring());
  std::wstring short_norm = normalize(short_profile);
  std::wstring actual_lower = lower(actual);
  std::wstring short_lower = lower(short_norm);
  if (!actual_lower.starts_with(short_lower)) return path;
  if (actual.size() > short_norm.size() && actual[short_norm.size()] != L'\\') {
    return path;
  }

  std::wstring profile_norm = normalize(profile);
  return std::filesystem::path(profile_norm + actual.substr(short_norm.size()));
}

// utf8_path()/path_utf8() now live in utils:utf8 so every command shares
// one implementation (niubash #88).

auto native_display_path(const std::filesystem::path& path) -> std::string {
  // generic_wstring(): forward slashes, matching the historical
  // generic_string() output shape, but encoded as UTF-8 (#88).
  return wstring_to_utf8(
      expand_user_profile_short_name(long_existing_prefix_path(path))
          .generic_wstring());
}

auto random_index(size_t limit) -> size_t {
  static thread_local std::mt19937 rng([] {
    std::array<std::uint32_t, 8> seed_data{};
    std::random_device rd;
    for (auto& value : seed_data) {
      value = rd();
    }
    seed_data[6] ^= static_cast<std::uint32_t>(GetCurrentProcessId());
    seed_data[7] ^= static_cast<std::uint32_t>(GetTickCount());
    std::seed_seq seed(seed_data.begin(), seed_data.end());
    return std::mt19937(seed);
  }());
  std::uniform_int_distribution<size_t> dist(0, limit - 1);
  return dist(rng);
}
auto default_temporary_directory() -> cp::Result<std::string> {
  if (auto tmpdir = getenv_path(L"TMPDIR")) {
    return *tmpdir;
  }

  std::wstring buffer(MAX_PATH, wchar_t{});
  DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
  if (length == 0) {
    return std::unexpected("failed to get temporary directory");
  }
  if (length >= buffer.size()) {
    buffer.assign(static_cast<size_t>(length) + 1, wchar_t{});
    length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (length == 0 || length >= buffer.size()) {
      return std::unexpected("failed to get temporary directory");
    }
  }

  buffer.resize(length);
  if (buffer.empty()) {
    return std::unexpected("failed to get temporary directory");
  }
  return native_display_path(std::filesystem::path(buffer));
}

// [GNU] the template (plus suffix) must contain a run of at least 3
// consecutive X's in its final path component.
auto has_x_run_of_three(std::string_view templ) -> bool {
  size_t run = 0;
  for (char c : templ) {
    if (c == 'X') {
      ++run;
      if (run >= 3) return true;
    } else {
      run = 0;
    }
  }
  return false;
}

auto build_config(const CommandContext<MKTEMP_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.make_directory =
      ctx.get<bool>("--directory", false) || ctx.get<bool>("-d", false);
  cfg.dry_run = ctx.get<bool>("--dry-run", false) || ctx.get<bool>("-u", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);

  auto tmpdir_opt = ctx.get<std::string>("--tmpdir", "");
  if (tmpdir_opt.empty()) {
    tmpdir_opt = ctx.get<std::string>("-p", "");
  }
  if (tmpdir_opt.empty() && ctx.count({"--tmpdir", "-p"}) > 0) {
    // [GNU] an explicitly empty --tmpdir falls back to the default
    // temporary directory (uutils #8445/#10189).
    auto default_tmpdir = default_temporary_directory();
    if (!default_tmpdir) {
      return std::unexpected(default_tmpdir.error());
    }
    tmpdir_opt = *default_tmpdir;
  }
  if (!tmpdir_opt.empty()) {
    cfg.tmpdir = tmpdir_opt;
  }

  // -t (deprecated): interpret TEMPLATE as a single file name component
  // relative to a directory: $TMPDIR, if set; else /tmp.
  cfg.use_tmpdir_flag = ctx.get<bool>("-t", false);

  cfg.suffix = ctx.get<std::string>("--suffix", "");
  if (cfg.suffix.find('/') != std::string::npos ||
      cfg.suffix.find('\\') != std::string::npos) {
    return std::unexpected("invalid suffix '" + cfg.suffix +
                           "', contains directory separator");
  }

  // Get template from positionals
  if (!ctx.positionals.empty()) {
    cfg.template_str = std::string(ctx.positionals[0]);
  } else {
    cfg.template_str = "tmp.XXXXXXXXXX";
    if (cfg.tmpdir.empty()) {
      auto default_tmpdir = default_temporary_directory();
      if (!default_tmpdir) {
        return std::unexpected(default_tmpdir.error());
      }
      cfg.tmpdir = *default_tmpdir;
    }
  }

  // -t flag: interpret template as single file name component, relative
  // to $TMPDIR (or default temp dir). Strip any directory from template.
  if (cfg.use_tmpdir_flag) {
    cfg.template_str = path_utf8(utf8_path(cfg.template_str).filename());
    if (cfg.template_str.empty()) {
      cfg.template_str = "tmp.XXXXXXXXXX";
    }
    if (cfg.tmpdir.empty()) {
      auto default_tmpdir = default_temporary_directory();
      if (!default_tmpdir) {
        return std::unexpected(default_tmpdir.error());
      }
      cfg.tmpdir = *default_tmpdir;
    }
  }

  // [GNU] reject templates without a run of >= 3 consecutive X's:
  // "too few X's in template '...'" (uutils #10187).
  {
    std::string full = cfg.template_str + cfg.suffix;
    std::string component =
        path_utf8(utf8_path(normalize_win_shell_path(full)).filename());
    if (component.empty()) {
      component = full;
    }
    if (!has_x_run_of_three(component)) {
      return std::unexpected("too few X's in template '" + full + "'");
    }
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  auto find_template_run =
      [](std::string_view templ) -> std::optional<std::pair<size_t, size_t>> {
    auto last_sep = templ.find_last_of("/\\");
    size_t component_start =
        last_sep == std::string_view::npos ? 0 : last_sep + 1;

    size_t best_start = std::string_view::npos;
    size_t best_len = 0;
    for (size_t i = component_start; i < templ.size();) {
      if (templ[i] != 'X') {
        ++i;
        continue;
      }
      size_t j = i;
      while (j < templ.size() && templ[j] == 'X') {
        ++j;
      }
      size_t len = j - i;
      if (len >= 3) {
        best_start = i;
        best_len = len;
      }
      i = j;
    }

    if (best_start == std::string_view::npos) {
      return std::nullopt;
    }
    return std::pair{best_start, best_len};
  };

  std::filesystem::path base_dir;
  std::string template_component = cfg.template_str;

  if (!cfg.tmpdir.empty()) {
    base_dir = utf8_path(normalize_win_shell_path(cfg.tmpdir));
    template_component = path_utf8(
        utf8_path(normalize_win_shell_path(cfg.template_str)).filename());
  } else {
    std::filesystem::path template_path =
        utf8_path(normalize_win_shell_path(cfg.template_str));
    base_dir = template_path.parent_path();
    template_component = path_utf8(template_path.filename());
  }

  if (template_component.empty()) {
    template_component = "tmp.XXXXXX";
  }

  // Process template. Replace the final X-run in the last path component and
  // append --suffix after that replacement, matching GNU/Microsoft usage such
  // as `mktemp file-XXXX.txt` and `mktemp --suffix=.txt file-XXXX`.
  std::string template_str = template_component;
  auto run_info = find_template_run(template_str);
  if (!run_info) {
    template_str += "XXXXXX";
    run_info = std::pair{template_str.size() - 6, size_t{6}};
  }
  template_str += cfg.suffix;
  auto [xxx_pos, xxx_len] = *run_info;

  // Generate unique filename
  std::string temp_file;
  int max_attempts = 100;

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    // Generate random characters for XXXXXX
    std::string filename = template_str;
    const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";

    for (size_t i = 0; i < xxx_len; ++i) {
      filename[xxx_pos + i] = charset[random_index(sizeof(charset) - 1)];
    }

    // Build full path
    std::filesystem::path candidate_path =
        base_dir.empty() ? utf8_path(filename)
                         : (base_dir / utf8_path(filename));
    temp_file = candidate_path.is_absolute()
                    ? native_display_path(candidate_path)
                    : path_utf8(candidate_path);

    // Check if file/directory already exists
    DWORD attrs = GetFileAttributesW(utf8_to_wstring(temp_file).c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      // File doesn't exist, we can use this name
      break;
    }

    // File exists, try again
    temp_file.clear();
  }

  if (temp_file.empty()) {
    if (!cfg.quiet) {
      cp::Result<int> result2 =
          std::unexpected("failed to create unique filename");
      cp::report_error(result2, L"mktemp");
    }
    return 1;
  }

  // Create file or directory (unless dry-run)
  if (!cfg.dry_run) {
    // [GNU] diagnostics keep the original template and the errno text:
    // "failed to create file via template 'T': No such file or directory"
    // (uutils #13720).
    if (cfg.make_directory) {
      if (!CreateDirectoryW(utf8_to_wstring(temp_file).c_str(), NULL)) {
        if (!cfg.quiet) {
          safeErrorPrintLn(winux::i18n::format(
              "command.mktemp.error.failed_create_directory_template",
              "mktemp: failed to create directory via template '{}': {}",
              cfg.template_str, win32_posix_error_text(GetLastError())));
        }
        return 1;
      }
    } else {
      // Create file
      errno = 0;
      std::ofstream f(utf8_path(temp_file), std::ios::binary);
      if (!f) {
        if (!cfg.quiet) {
          const int err = errno != 0 ? errno : ENOENT;
          safeErrorPrintLn(winux::i18n::format(
              "command.mktemp.error.failed_create_file_template",
              "mktemp: failed to create file via template '{}': {}",
              cfg.template_str, std::string(std::strerror(err))));
        }
        return 1;
      }
      f.close();
    }
  }

  safePrintLn(temp_file);
  return 0;
}

}  // namespace mktemp_pipeline

REGISTER_COMMAND(
    mktemp, "mktemp", "mktemp [OPTION]... [TEMPLATE]",
    "Create a temporary file or directory, safely, and print its name.\n"
    "\n"
    "TEMPLATE must contain at least 3 consecutive 'X's in last component.\n"
    "If TEMPLATE is not specified, use tmp.XXXXXXXXXX instead.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This implementation uses Windows GetTempFileName API.\n"
    "Custom templates are partially supported.",
    "  mktemp                          # create temp file\n"
    "  mktemp -d                        # create temp directory\n"
    "  mktemp -u                        # just print name\n"
    "  mktemp -t /tmp                   # use specific directory\n"
    "  mktemp --quiet                   # suppress errors",
    "tempfile(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MKTEMP_OPTIONS) {
  using namespace mktemp_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"mktemp");
    return 1;
  }

  return run(*cfg_result);
}
