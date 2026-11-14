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

// [GNU] mktemp.c:276-311 prints file_name_concat (dest_dir, template): the
// caller's directory spec verbatim, never canonicalized. A -p/--tmpdir
// operand in MSYS drive form (/d/... or /cygdrive/d/...) must therefore
// echo back in that dialect so shell round-trips like
// t=$(mktemp -p "$d" 'x.XXXXXX'); mv "$t" "$d/out" stay dialect-consistent
// (#1141). Filesystem operations keep using the normalized native form.
// The default paths (TMPDIR, GetTempPathW) stay native-form, matching the
// pinned mktemp_default_template_uses_tmpdir_env_as_native_path behavior.
enum class DisplayDialect {
  Native,     // Windows drive form (D:/...)
  MsysDrive,  // /d/...
  Cygdrive,   // /cygdrive/d/...
};

auto display_dialect_of(std::string_view text) -> DisplayDialect {
  auto is_separator = [](char ch) { return ch == '/' || ch == '\\'; };
  if (text.size() >= 2 && is_separator(text[0]) &&
      std::isalpha(static_cast<unsigned char>(text[1])) &&
      (text.size() == 2 || is_separator(text[2]))) {
    return DisplayDialect::MsysDrive;
  }
  constexpr std::string_view cygdrive = "/cygdrive/";
  if (text.size() >= cygdrive.size() + 1 && text.starts_with(cygdrive) &&
      std::isalpha(static_cast<unsigned char>(text[cygdrive.size()])) &&
      (text.size() == cygdrive.size() + 1 ||
       is_separator(text[cygdrive.size() + 1]))) {
    return DisplayDialect::Cygdrive;
  }
  return DisplayDialect::Native;
}

// Re-express a native drive path (D:/rest) in the caller's dialect, using
// the MSYS lowercase-drive convention (/d/rest, /cygdrive/d/rest). Anything
// that is not a drive-letter path is returned unchanged.
auto to_display_dialect(std::string_view native, DisplayDialect dialect)
    -> std::string {
  if (dialect == DisplayDialect::Native || native.size() < 2 ||
      native[1] != ':') {
    return std::string(native);
  }
  const char drive =
      static_cast<char>(std::tolower(static_cast<unsigned char>(native[0])));
  std::string prefix =
      dialect == DisplayDialect::MsysDrive ? "/" : "/cygdrive/";
  prefix.push_back(drive);
  return prefix + std::string(native.substr(2));
}

struct Config {
  bool make_directory = false;
  bool dry_run = false;
  bool quiet = false;
  bool use_tmpdir_flag = false;
  bool dest_dir_mode = false;
  std::string tmpdir;
  std::string template_str;
  std::string suffix;
  DisplayDialect tmpdir_dialect = DisplayDialect::Native;
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

  if (text.size() >= 2 && (text[0] == '/' || text[0] == '\\') &&
      std::isalpha(static_cast<unsigned char>(text[1])) &&
      (text.size() == 2 || text[2] == '/' || text[2] == '\\')) {
    // A bare drive root ("/e") names "E:\" like the "/e/..." deep form;
    // joining must keep a root separator so the result is not "E:<rel>"
    // (#1230).
    if (text.size() == 2) return make_drive_path(text[1], "/");
    return make_drive_path(text[1], text.substr(2));
  }

  constexpr std::string_view cygdrive = "/cygdrive/";
  if (text.size() >= cygdrive.size() + 1 && text.starts_with(cygdrive) &&
      std::isalpha(static_cast<unsigned char>(text[cygdrive.size()])) &&
      (text.size() == cygdrive.size() + 1 ||
       text[cygdrive.size() + 1] == '/' ||
       text[cygdrive.size() + 1] == '\\')) {
    return make_drive_path(text[cygdrive.size()],
                           text.size() == cygdrive.size() + 1
                               ? "/"
                               : text.substr(cygdrive.size() + 1));
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
    cfg.tmpdir_dialect = display_dialect_of(tmpdir_opt);
  }

  // -t (deprecated): interpret TEMPLATE as a single file name component
  // relative to a directory: $TMPDIR, if set; else /tmp.
  cfg.use_tmpdir_flag = ctx.get<bool>("-t", false);

  // [GNU] More than one template operand is an error, and it is
  // reported before any suffix/template validation (mktemp.c) (#1358).
  if (ctx.positionals.size() > 1) {
    return std::unexpected(
        "too many templates\nTry 'mktemp --help' for more information.");
  }

  cfg.suffix = ctx.get<std::string>("--suffix", "");
  cfg.dest_dir_mode =
      ctx.count({"--tmpdir", "-p"}) > 0 || ctx.positionals.empty();

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

  // [GNU] The X-run rules, in mktemp.c's order (222-258), applied to
  // the template exactly as given:
  //  * with --suffix, the template must END in X (checked first);
  //    the "suffix" whose separator is policed is the region after
  //    the last X of the combined string;
  //  * without --suffix, the region after the template's last X is
  //    the implicit suffix: a separator there is the same error;
  //  * the X count is the run ENDING where that suffix begins, so
  //    'XXXXaXb' has a run of 1 and is rejected.
  if (!cfg.suffix.empty()) {
    if (cfg.template_str.empty() || cfg.template_str.back() != 'X') {
      return std::unexpected("with --suffix, template '" +
                             cfg.template_str + "' must end in X");
    }
    if (cfg.suffix.find('/') != std::string::npos ||
        cfg.suffix.find('\\') != std::string::npos) {
      return std::unexpected("invalid suffix '" + cfg.suffix +
                             "', contains directory separator");
    }
    size_t run = 0;
    for (size_t i = cfg.template_str.size();
         i > 0 && cfg.template_str[i - 1] == 'X'; --i) {
      ++run;
    }
    if (run < 3) {
      return std::unexpected("too few X's in template '" +
                             cfg.template_str + cfg.suffix + "'");
    }
  } else {
    size_t last_x = cfg.template_str.find_last_of('X');
    std::string tail =
        last_x == std::string::npos ? "" : cfg.template_str.substr(last_x + 1);
    if (tail.find('/') != std::string::npos) {
      return std::unexpected("invalid suffix '" + tail +
                             "', contains directory separator");
    }
    size_t run = 0;
    size_t upto = last_x == std::string::npos ? 0 : last_x + 1;
    for (size_t i = upto; i > 0 && cfg.template_str[i - 1] == 'X'; --i) {
      ++run;
    }
    if (run < 3) {
      return std::unexpected("too few X's in template '" +
                             cfg.template_str + "'");
    }
  }

  // [GNU] Destination-directory rules (mktemp.c:260-295), after the
  // X validation.
  if (cfg.use_tmpdir_flag) {
    // -t: $TMPDIR beats a -p DIR, which beats the default (the old
    // code silently stripped any directory from the template
    // instead of diagnosing it, and let -p beat $TMPDIR).
    if (auto env_dir = getenv_path(L"TMPDIR")) {
      cfg.tmpdir = *env_dir;
      cfg.tmpdir_dialect = display_dialect_of(cfg.tmpdir);
    } else if (cfg.tmpdir.empty()) {
      auto default_tmpdir = default_temporary_directory();
      if (!default_tmpdir) {
        return std::unexpected(default_tmpdir.error());
      }
      cfg.tmpdir = *default_tmpdir;
    }
    if (cfg.template_str.find('/') != std::string::npos) {
      return std::unexpected("invalid template, '" + cfg.template_str +
                             "', contains directory separator");
    }
  } else if (cfg.dest_dir_mode) {
    const std::string& t = cfg.template_str;
    bool absolute = !t.empty() && (t.front() == '/' || t.front() == '\\' ||
                                   (t.size() >= 2 &&
                                    std::isalpha(static_cast<unsigned char>(
                                        t[0])) &&
                                    t[1] == ':'));
    if (absolute) {
      return std::unexpected("invalid template, '" + t +
                             "'; with --tmpdir, it may not be absolute");
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

  // #1364: the directory side of the template is an argv operand: fold it
  // through the shared normalize_api_operand boundary after the local
  // drive-dialect fold, so a shell-namespace spelling (the bare "/", /tmp,
  // ...) resolves through the shell tree (payload root) instead of being
  // absolutized onto the current drive by GetFullPathNameW downstream.
  const auto fold_dir_operand = [](std::string_view text) -> std::string {
    return native_path::normalize_api_operand(
        normalize_win_shell_path(text));
  };

  if (!cfg.tmpdir.empty()) {
    base_dir = utf8_path(fold_dir_operand(cfg.tmpdir));
    // [GNU] mktemp.c:297-299 (file_name_concat): with a destination
    // directory the template is appended as given -- interior
    // separators are preserved ('-p DIR sub/XXXXXX' creates inside
    // sub/), not truncated to the final component.
    template_component = cfg.template_str;
  } else {
    std::filesystem::path template_path =
        utf8_path(fold_dir_operand(cfg.template_str));
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

  // [GNU] mktemp.c:301-325: a creation failure is diagnosed with
  // the FULL destination template (directory + template, X's
  // intact), never the bare template operand.
  std::filesystem::path dest_template_path =
      base_dir.empty() ? utf8_path(template_str)
                       : (base_dir / utf8_path(template_str));
  std::string dest_template = dest_template_path.is_absolute()
                                  ? native_display_path(dest_template_path)
                                  : path_utf8(dest_template_path);
  dest_template = to_display_dialect(dest_template, cfg.tmpdir_dialect);

  // Generate a unique name and create it.  Creation is atomic
  // (CREATE_NEW / CreateDirectoryW): the old check-then-create
  // sequence (exists probe, then a truncating ofstream) could
  // claim a name another process had just taken.
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

    if (cfg.dry_run) {
      DWORD attrs = GetFileAttributesW(utf8_to_wstring(temp_file).c_str());
      if (attrs == INVALID_FILE_ATTRIBUTES) {
        break;  // Name is free; nothing is created in dry-run mode.
      }
      temp_file.clear();
      continue;
    }

    DWORD create_error = ERROR_SUCCESS;
    if (cfg.make_directory) {
      if (CreateDirectoryW(utf8_to_wstring(temp_file).c_str(), NULL)) {
        break;
      }
      create_error = GetLastError();
    } else {
      HANDLE handle = CreateFileW(utf8_to_wstring(temp_file).c_str(),
                                  GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        break;
      }
      create_error = GetLastError();
    }
    if (create_error == ERROR_FILE_EXISTS ||
        create_error == ERROR_ALREADY_EXISTS) {
      temp_file.clear();
      continue;  // Lost the race for this name; draw another.
    }

    if (!cfg.quiet) {
      if (cfg.make_directory) {
        safeErrorPrintLn(winux::i18n::format(
            "command.mktemp.error.failed_create_directory_template",
            "mktemp: failed to create directory via template '{}': {}",
            dest_template, win32_posix_error_text(create_error)));
      } else {
        safeErrorPrintLn(winux::i18n::format(
            "command.mktemp.error.failed_create_file_template",
            "mktemp: failed to create file via template '{}': {}",
            dest_template, win32_posix_error_text(create_error)));
      }
    }
    return 1;
  }

  if (temp_file.empty()) {
    if (!cfg.quiet) {
      cp::Result<int> result2 =
          std::unexpected("failed to create unique filename");
      cp::report_error(result2, L"mktemp");
    }
    return 1;
  }

  // [GNU] The printed name keeps the -p/--tmpdir dialect the caller used
  // (#1141); creation above happened on the normalized native form.
  safePrintLn(to_display_dialect(temp_file, cfg.tmpdir_dialect));
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
