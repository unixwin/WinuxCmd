// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
///   - @description:
/// @Description: Implemention for mv.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

/**
 * @brief MV command options definition
 *
 * This array defines all the options supported by the mv command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -b: Like --backup but does not accept an argument [IMPLEMENTED]
 * - @a -f, @a --force: Do not prompt before overwriting [IMPLEMENTED]
 * - @a -i: Prompt before overwrite [IMPLEMENTED]
 * - @a -n, @a --no-clobber: Do not overwrite an existing file [IMPLEMENTED]
 * - @a --strip-trailing-slashes: Remove any trailing slashes from each SOURCE
 * argument [IMPLEMENTED]
 * - @a -S, @a --suffix: Override the usual backup suffix [IMPLEMENTED]
 * - @a -t, @a --target-directory: Move all SOURCE arguments into DIRECTORY
 * [IMPLEMENTED]
 * - @a -T, @a --no-target-directory: Treat DEST as a normal file [IMPLEMENTED]
 * - @a -u: Move only when the SOURCE file is newer than the destination file or
 * when the destination file is missing [IMPLEMENTED]
 * - @a -v, @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a -Z, @a --context: Set SELinux security context of destination file to
 * default type [TODO]
 * - @a --backup: Make a backup of each existing destination file [IMPLEMENTED]
 * - @a --force: Do not prompt before overwriting [IMPLEMENTED]
 * - @a --interactive: Prompt according to WHEN: never, once, or always
 * (-i) [IMPLEMENTED]
 * - @a --no-clobber: Do not overwrite an existing file [IMPLEMENTED]
 * - @a --suffix: Override the usual backup suffix [IMPLEMENTED]
 * - @a --target-directory: Move all SOURCE arguments into DIRECTORY
 * [IMPLEMENTED]
 * - @a --no-target-directory: Treat DEST as a normal file [IMPLEMENTED]
 * - @a --update: Move only when the SOURCE file is newer than the destination
 * file or when the destination file is missing [IMPLEMENTED]
 * - @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a --context: Set SELinux security context of destination file to default
 */

#include "pch/pch.h"
#pragma comment(lib, "shlwapi.lib")
#include "core/command_macros.h"
#include <winioctl.h>  // For FSCTL_GET_REPARSE_POINT (junction targets)

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// clang-format off
auto constexpr MV_OPTIONS =
    std::array{
               // [GNU]
               OPTION("-b", "", "like --backup but does not accept an argument"),
               // [GNU] --debug: explain how a file is moved
               OPTION("", "--debug", "explain how a file is moved"),
               // [DIFFERS] --exchange is emulated via non-atomic renames
               OPTION("", "--exchange", "exchange source and destination"),
               // [GNU]
               OPTION("-f", "--force", "do not prompt before overwriting"),
               // [GNU]
               OPTION("-i", "", "prompt before overwrite"),
               // [GNU] mv has no -I option (that is rm's); the "once" prompt
               // mode is reached only via --interactive=once (#1355).
               // [GNU]
               OPTION("-n", "--no-clobber", "do not overwrite an existing file"),
               // [GNU]
               OPTION("", "--no-copy", "do not copy if renaming fails"),
               // [GNU]
               OPTION("", "--strip-trailing-slashes", "remove any trailing slashes from each SOURCE argument"),
               // [GNU]
               OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
               // [GNU]
               OPTION("-t", "--target-directory", "move all SOURCE arguments into DIRECTORY", STRING_TYPE),
               // [GNU]
               OPTION("-T", "--no-target-directory", "treat DEST as a normal file"),
               // [GNU]
               OPTION("-u", "", "equivalent to --update[=older]"),
               // [GNU]
               OPTION("-v", "--verbose", "explain what is being done"),
               // [DIFFERS]
               OPTION("-Z", "--context", "set SELinux security context of destination file to default type"),
               // [GNU]
               OPTION("", "--backup", "make a backup of each existing destination file", OPTIONAL_STRING_TYPE),
               // [GNU]
               OPTION("", "--interactive", "prompt according to WHEN: never, once, or always (-i)", OPTIONAL_STRING_TYPE),
               // [GNU]
               OPTION("", "--no-clobber", "do not overwrite an existing file"),
               // [GNU]
               OPTION("", "--suffix", "override the usual backup suffix", STRING_TYPE),
               // [GNU]
               OPTION("", "--target-directory", "move all SOURCE arguments into DIRECTORY", STRING_TYPE),
               // [GNU]
               OPTION("", "--no-target-directory", "treat DEST as a normal file"),
               // [GNU]
               OPTION("", "--update", "control which existing files are updated; UPDATE={all,none,older(default)}", OPTIONAL_STRING_TYPE),
               // [GNU]
               OPTION("", "--verbose", "explain what is being done"),
               // [DIFFERS]
               OPTION("", "--context", "set SELinux security context of destination file to default type")};
// clang-format on

// ======================================================
// Pipeline components
// ======================================================
namespace mv_pipeline {
namespace cp = core::pipeline;

// --update[=UPDATE] mode: controls which existing files are replaced.
enum class UpdateMode {
  all,    // always replace (default without --update)
  older,  // replace only if source is newer (-u, --update, --update=older)
  none,   // never replace (--update=none, similar to --no-clobber)
};

struct MoveContext {
  SmallVector<std::string, 64> source_paths;
  std::string dest_path;
  bool target_directory_option = false;
  bool no_target_directory = false;
  UpdateMode update_mode = UpdateMode::all;
};

enum class OverwriteMode {
  default_mode,
  force,
  interactive_once,
  interactive_always,
  no_clobber,
};

// --update[=UPDATE] mode: controls which existing files are replaced.

template <size_t N>
auto parse_overwrite_mode(const CommandContext<N>& ctx)
    -> cp::Result<OverwriteMode> {
  OverwriteMode mode = OverwriteMode::default_mode;

  // [GNU] Judge the overwrite mode from the parser's option occurrences,
  // not from a raw argv rescan: the parser has already expanded bundled
  // short flags into separate occurrences and consumed option values
  // (attached or separate), so a value that merely contains the letters
  // f/i/I/n — e.g. `mv -t ndir s` or `mv --suffix n …` — can no longer
  // rewrite the mode (#1325).  Last applicable option wins.
  if (!ctx.metas) {
    return mode;
  }
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= ctx.metas->size()) {
      continue;
    }
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.short_name == "-f" || meta.long_name == "--force") {
      mode = OverwriteMode::force;
    } else if (meta.short_name == "-i") {
      mode = OverwriteMode::interactive_always;
    } else if (meta.short_name == "-n" || meta.long_name == "--no-clobber") {
      mode = OverwriteMode::no_clobber;
    } else if (meta.long_name == "--interactive") {
      auto value = std::get_if<std::string>(&occurrence.value);
      const std::string when = value ? *value : std::string{};
      if (when.empty() || when == "always") {
        mode = OverwriteMode::interactive_always;
      } else if (when == "never") {
        mode = OverwriteMode::force;
      } else if (when == "once") {
        mode = OverwriteMode::interactive_once;
      } else {
        return std::unexpected("invalid argument '" + when +
                               "' for '--interactive'");
      }
    }
  }

  return mode;
}

auto append_expanded_source(SmallVector<std::string, 64>& source_paths,
                            std::string_view arg) -> void {
  std::string source(arg);
  if (contains_wildcard(source)) {
    auto glob_result = glob_expand(source);
    if (glob_result.expanded) {
      for (const auto& file : glob_result.files) {
        source_paths.push_back(wstring_to_utf8(file));
      }
      return;
    }
  }
  source_paths.push_back(std::move(source));
}

auto strip_trailing_slashes(std::string path) -> std::string {
  while (path.size() > 1 && (path.back() == '\\' || path.back() == '/')) {
    if (path.size() == 3 && path[1] == ':') break;
    path.pop_back();
  }
  return path;
}

enum class MvBackupControl { none, simple, numbered, existing };

struct MvBackupParse {
  MvBackupControl control = MvBackupControl::none;
  int error_kind = 0;  // 0 = ok, 1 = invalid, 2 = ambiguous
};

// [GNU] --backup CONTROL follows argmatch via xget_version (mv.c:519-522):
// exact name or unique prefix of none/off, simple/never, existing/nil,
// numbered/t; case-sensitive. A bare -b/--backup falls back to the
// VERSION_CONTROL environment variable, then to 'existing'.
auto parse_mv_backup_control(const std::string& raw) -> MvBackupParse {
  static constexpr std::pair<std::string_view, MvBackupControl> kNames[] = {
      {"none", MvBackupControl::none},
      {"off", MvBackupControl::none},
      {"simple", MvBackupControl::simple},
      {"never", MvBackupControl::simple},
      {"existing", MvBackupControl::existing},
      {"nil", MvBackupControl::existing},
      {"numbered", MvBackupControl::numbered},
      {"t", MvBackupControl::numbered},
  };
  for (auto [name, control] : kNames) {
    if (raw == name) return {control, 0};
  }
  if (raw.empty()) {
    if (const char* env = std::getenv("VERSION_CONTROL")) {
      return parse_mv_backup_control(std::string(env));
    }
    return {MvBackupControl::existing, 0};
  }
  int matches = 0;
  MvBackupControl matched = MvBackupControl::none;
  for (auto [name, control] : kNames) {
    if (name.starts_with(raw)) {
      ++matches;
      matched = control;
    }
  }
  if (matches == 1) return {matched, 0};
  return {MvBackupControl::none, matches == 0 ? 1 : 2};
}

auto mv_backup_type_message(int error_kind, const std::string& value)
    -> std::string {
  std::string message =
      error_kind == 2 ? "ambiguous argument '" : "invalid argument '";
  message += value;
  message +=
      "' for 'backup type'\n"
      "Valid arguments are:\n"
      "  - 'none', 'off'\n"
      "  - 'simple', 'never'\n"
      "  - 'existing', 'nil'\n"
      "  - 'numbered', 't'\n"
      "Try 'mv --help' for more information.";
  return message;
}

// [GNU] Whether backups are in effect at all: -b, --backup, or -S
// (mv.c:404-407: -S sets make_backups). The control value comes from
// --backup when given.  Judged from the parser's option occurrences,
// like parse_overwrite_mode: with this table's duplicated long-only
// entries, the name-based has()/get() probes reported backups as
// given for a bare -n (the subject answered `mv -n a b` with the
// --backup/--no-clobber mutual-exclusion error and made backups on
// plain overwrites), while the occurrence scan reads the same
// options correctly.
auto mv_make_backups(const CommandContext<MV_OPTIONS.size()>& ctx)
    -> cp::Result<std::optional<MvBackupControl>> {
  bool given = false;
  std::string raw;
  if (ctx.metas) {
    for (const auto& occurrence : ctx.options.occurrences()) {
      if (occurrence.index >= ctx.metas->size()) continue;
      const auto& meta = (*ctx.metas)[occurrence.index];
      if (meta.short_name == "-b" || meta.long_name == "--backup" ||
          meta.short_name == "-S" || meta.long_name == "--suffix") {
        given = true;
      }
      if (meta.long_name == "--backup") {
        if (auto value = std::get_if<std::string>(&occurrence.value)) {
          raw = *value;
        }
      }
    }
  }
  if (!given) return std::nullopt;
  auto parsed = parse_mv_backup_control(raw);
  if (parsed.error_kind != 0) {
    return std::unexpected(mv_backup_type_message(parsed.error_kind, raw));
  }
  if (parsed.control == MvBackupControl::none) return std::nullopt;
  return parsed.control;
}

// [GNU] Whether any backup option (-b, --backup in any form including
// =none/=off, -S, --suffix) was given at all.  mv.c's conflict between
// backups and --no-clobber keys on this givenness, not on the resolved
// mode: verified against the oracle, `mv --backup=none -n a b` answers
// with the mutual-exclusion error just like `--backup=simple` does,
// while the backup itself is of course only made for an engaged mode.
auto mv_backup_option_given(const CommandContext<MV_OPTIONS.size()>& ctx)
    -> bool {
  if (!ctx.metas) return false;
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= ctx.metas->size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.short_name == "-b" || meta.long_name == "--backup" ||
        meta.short_name == "-S" || meta.long_name == "--suffix") {
      return true;
    }
  }
  return false;
}

// [GNU] Highest N among existing "<path>.~N~" siblings, or 0 when none
// (numbered backups take max + 1).
auto mv_max_numbered_backup(const std::wstring& target) -> int {
  std::filesystem::path target_path(target);
  std::filesystem::path dir = target_path.parent_path();
  if (dir.empty()) dir = ".";
  const std::string base = wstring_to_utf8(target_path.filename().wstring());
  const std::string prefix = base + ".~";
  int max_version = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    const std::string name =
        wstring_to_utf8(entry.path().filename().wstring());
    if (!name.starts_with(prefix) || name.size() <= prefix.size() + 1 ||
        name.back() != '~') {
      continue;
    }
    const std::string digits =
        name.substr(prefix.size(), name.size() - prefix.size() - 1);
    if (!std::ranges::all_of(digits, [](unsigned char ch) {
          return std::isdigit(ch) != 0;
        })) {
      continue;
    }
    int version = 0;
    for (char ch : digits) version = version * 10 + (ch - '0');
    if (version > max_version) max_version = version;
  }
  return max_version;
}

auto parse_arguments(const CommandContext<MV_OPTIONS.size()>& ctx)
    -> cp::Result<MoveContext> {
  MoveContext move_ctx;

  // [GNU] A repeated -t is fatal during option processing (mv.c:367-370).
  if (ctx.count({"-t", "--target-directory"}) > 1) {
    return std::unexpected(
        "multiple target directories specified\n"
        "Try 'mv --help' for more information.");
  }

  // Get target directory if specified
  std::string target_dir = ctx.get<std::string>("--target-directory", "");
  if (target_dir.empty()) {
    target_dir = ctx.get<std::string>("-t", "");
  }
  move_ctx.no_target_directory = ctx.get<bool>("-T", false) ||
                                 ctx.get<bool>("--no-target-directory", false);
  if (!target_dir.empty() && move_ctx.no_target_directory) {
    // [GNU] mv.c:445-449, with the short forms named.
    return std::unexpected(
        "cannot combine --target-directory (-t) and "
        "--no-target-directory (-T)");
  }

  // Parse --update[=UPDATE] mode
  // [GNU] Explicit --update=VALUE forms are last-wins among themselves;
  // -u and a bare --update are weak: they select 'older' only when no
  // explicit --update=VALUE appears anywhere on the command line
  // (coreutils 9.4 measured; #1326).  A bare --update is indistinguishable
  // from --update= in the parser (both store ""), so "" stays 'older'
  // here; GNU rejects --update= as ambiguous — a documented gap.
  {
    std::optional<std::string> explicit_update;
    bool weak_older = ctx.get<bool>("-u", false);
    for (const auto& occ : ctx.string_occurrences({"--update"})) {
      if (!occ.value.empty()) {
        explicit_update = occ.value;
      } else {
        weak_older = true;
      }
    }
    if (explicit_update.has_value()) {
      // [GNU] argmatch: exact name or unique prefix of all/none/older
      // (mv.c:55-63, 375-400).
      static constexpr std::pair<std::string_view, UpdateMode> kUpdateNames[] =
          {{"all", UpdateMode::all},
           {"none", UpdateMode::none},
           {"older", UpdateMode::older}};
      std::optional<UpdateMode> matched;
      int matches = 0;
      for (auto [name, mode] : kUpdateNames) {
        if (*explicit_update == name) {
          matched = mode;
          matches = 1;
          break;
        }
      }
      if (!matched) {
        for (auto [name, mode] : kUpdateNames) {
          if (name.starts_with(*explicit_update)) {
            ++matches;
            matched = mode;
          }
        }
      }
      if (matched && matches == 1) {
        move_ctx.update_mode = *matched;
      } else {
        return std::unexpected(
            std::string(matches > 1 ? "ambiguous argument '" : "invalid argument '") +
            *explicit_update +
            "' for '--update'\n"
            "Valid arguments are:\n"
            "  - 'all'\n"
            "  - 'none'\n"
            "  - 'older'\n"
            "Try 'mv --help' for more information.");
      }
    } else if (weak_older) {
      move_ctx.update_mode = UpdateMode::older;
    }
  }

  bool strip_slashes = ctx.get<bool>("--strip-trailing-slashes", false);
  if (!target_dir.empty()) {
    move_ctx.target_directory_option = true;
    move_ctx.dest_path = target_dir;
    for (auto arg : ctx.positionals) {
      auto src = std::string(arg);
      if (strip_slashes) src = strip_trailing_slashes(std::move(src));
      append_expanded_source(move_ctx.source_paths, src);
    }
  } else {
    // Regular case: last argument is destination
    if (ctx.positionals.empty()) {
      return std::unexpected("missing file operand");
    }
    if (ctx.positionals.size() == 1) {
      // [GNU] mv.c:430-438: one operand names the missing destination.
      return std::unexpected("missing destination file operand after '" +
                             std::string(ctx.positionals[0]) + "'");
    }
    if (move_ctx.no_target_directory && ctx.positionals.size() > 2) {
      // [GNU] mv.c:450-453: -T admits exactly two operands.
      return std::unexpected("extra operand '" +
                             std::string(ctx.positionals[2]) + "'\n" +
                             "Try 'mv --help' for more information.");
    }

    for (size_t i = 0; i < ctx.positionals.size() - 1; ++i) {
      auto src = std::string(ctx.positionals[i]);
      if (strip_slashes) src = strip_trailing_slashes(std::move(src));
      append_expanded_source(move_ctx.source_paths, src);
    }
    move_ctx.dest_path = std::string(ctx.positionals.back());
  }

  if (move_ctx.source_paths.empty()) {
    return std::unexpected("missing file operand");
  }

  return move_ctx;
}

// [GNU] mv operands are pathnames: on this platform they arrive either in
// Windows drive form (D:/x) or MSYS drive form (/d/x, /cygdrive/d/x), and
// both denote the same file — GNU coreutils under MSYS accepts either.
// Route every Win32 call through the shared API-path boundary (the same
// invariant cp/install use) so a POSIX-form operand is not mistaken for a
// current-drive root-relative path (\d\x on the current drive) and fail
// ENOENT (#1140). Diagnostics keep quoting the operand as typed.
auto api_operand_w(std::string_view path) -> std::wstring {
  return native_path::make_api_path_operand(path).extended;
}

// True when candidate equals base or lives somewhere below it, comparing
// case-insensitively on the normalized backslash-separated forms (NTFS is
// case-insensitive by default).  Used to reject moving a directory into
// itself before the recursive-copy fallback could loop (#1140).
auto path_is_at_or_below(const std::wstring& base,
                         const std::wstring& candidate) -> bool {
  if (base.empty() || candidate.size() < base.size()) return false;
  if (_wcsnicmp(base.c_str(), candidate.c_str(), base.size()) != 0) {
    return false;
  }
  return candidate.size() == base.size() ||
         candidate[base.size()] == L'\\' || candidate[base.size()] == L'/';
}

auto check_path_exists(const std::string& path) -> cp::Result<bool> {
  std::wstring wpath = api_operand_w(path);
  DWORD attr = GetFileAttributesW(wpath.c_str());
  return attr != INVALID_FILE_ATTRIBUTES;
}

auto check_is_directory(const std::string& path) -> cp::Result<bool> {
  std::wstring wpath = api_operand_w(path);
  DWORD attr = GetFileAttributesW(wpath.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    return std::unexpected("cannot access '" + path +
                           "': No such file or directory");
  }
  return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

auto build_dest_path(const std::string& src_path, const std::string& dest_path,
                     bool dest_is_dir) -> cp::Result<std::string> {
  if (!dest_is_dir) {
    return dest_path;
  }

  std::wstring wsrc_path = utf8_to_wstring(src_path);
  LPWSTR file_name = PathFindFileNameW(wsrc_path.c_str());

  // OPTIMIZED: Use stack buffer instead of dynamic allocation
  char file_name_buf[MAX_PATH * 3];  // UTF-8 can be up to 3 bytes per char
  int file_name_length =
      WideCharToMultiByte(CP_UTF8, 0, file_name, -1, file_name_buf,
                          sizeof(file_name_buf), NULL, NULL);

  if (file_name_length > 0 && file_name_length < sizeof(file_name_buf)) {
    return dest_path + "\\" + std::string(file_name_buf);
  }

  // WideCharToMultiByte failed; fall back to raw wstring to avoid empty
  // filename
  return dest_path + "\\" + wstring_to_utf8(std::wstring(file_name));
}

auto confirm_overwrite(const std::string& dest_path) -> cp::Result<bool> {
  // OPTIMIZED: Avoid wstring concatenation
  safeErrorPrint("mv: overwrite '");
  safeErrorPrint(dest_path);
  safeErrorPrint("'? ");
  char response;
  std::cin.get(response);
  std::cin.ignore(1024, '\n');
  return response == 'y' || response == 'Y';
}

auto is_source_newer(const std::wstring& src_path,
                     const std::wstring& dest_path) -> bool {
  WIN32_FILE_ATTRIBUTE_DATA src_data{};
  WIN32_FILE_ATTRIBUTE_DATA dest_data{};
  if (!GetFileAttributesExW(src_path.c_str(), GetFileExInfoStandard,
                            &src_data)) {
    return true;
  }
  if (!GetFileAttributesExW(dest_path.c_str(), GetFileExInfoStandard,
                            &dest_data)) {
    return true;
  }
  return CompareFileTime(&src_data.ftLastWriteTime,
                         &dest_data.ftLastWriteTime) > 0;
}

// [GNU] Make the backup of an existing destination honouring the
// --backup CONTROL (mv.c:519-522); the value was previously ignored
// and every backup was a simple suffix backup. Returns the backup's
// display path when one was made (for the verbose line,
// copy.c:2085-2088).
auto backup_existing_destination(
    const std::wstring& dest_path, const std::string& dest_display,
    const CommandContext<MV_OPTIONS.size()>& ctx)
    -> cp::Result<std::optional<std::string>> {
  auto control = mv_make_backups(ctx);
  if (!control) return std::unexpected(control.error());
  // The engagement test is on the optional inside the Result, not on
  // the Result itself: has_value() on std::expected only says "no
  // error" and is true even when no backup was requested, which made
  // every overwrite create one.  GNU makes a backup only when one is
  // requested (mv.c:315 make_backups defaults to false; mv.c:519-522
  // backup_type stays no_backups otherwise).
  if (!control->has_value()) return std::nullopt;
  if (GetFileAttributesW(dest_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return std::nullopt;
  }

  // Last -S/--suffix occurrence wins (occurrence scan, as in
  // mv_make_backups).
  std::string suffix;
  if (ctx.metas) {
    for (const auto& occurrence : ctx.options.occurrences()) {
      if (occurrence.index >= ctx.metas->size()) continue;
      const auto& meta = (*ctx.metas)[occurrence.index];
      if (meta.short_name == "-S" || meta.long_name == "--suffix") {
        if (auto value = std::get_if<std::string>(&occurrence.value)) {
          suffix = *value;
        }
      }
    }
  }
  if (suffix.empty()) suffix = "~";

  std::wstring backup_path;
  std::string backup_display;
  if (**control == MvBackupControl::numbered ||
      (**control == MvBackupControl::existing &&
       mv_max_numbered_backup(dest_path) > 0)) {
    const std::string numbered =
        ".~" + std::to_string(mv_max_numbered_backup(dest_path) + 1) + "~";
    backup_path = dest_path + utf8_to_wstring(numbered);
    backup_display = dest_display + numbered;
  } else {
    backup_path = dest_path + utf8_to_wstring(suffix);
    backup_display = dest_display + suffix;
  }
  if (!MoveFileExW(dest_path.c_str(), backup_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    return std::unexpected("cannot create backup for destination");
  }
  return backup_display;
}

namespace mv_win32_compat {
// Same layout as REPARSE_DATA_BUFFER, spelled out so the mount-point fields
// stay readable (mirrors cp.cpp's reader).
struct ReparseDataBuffer {
  DWORD ReparseTag;
  WORD ReparseDataLength;
  WORD Reserved;
  union {
    struct {
      WORD SubstituteNameOffset;
      WORD SubstituteNameLength;
      WORD PrintNameOffset;
      WORD PrintNameLength;
      DWORD Flags;
      wchar_t PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      WORD SubstituteNameOffset;
      WORD SubstituteNameLength;
      WORD PrintNameOffset;
      WORD PrintNameLength;
      wchar_t PathBuffer[1];
    } MountPointReparseBuffer;
  };
};
}  // namespace mv_win32_compat

auto strip_nt_prefix(std::wstring_view path) -> std::wstring {
  for (std::wstring_view prefix :
       {std::wstring_view(L"\\??\\"), std::wstring_view(L"\\\\?\\")}) {
    if (path.starts_with(prefix)) {
      return std::wstring(path.substr(prefix.size()));
    }
  }
  return std::wstring(path);
}

// [GNU] #1365: read a reparse point's link text verbatim — the exact string
// stored in the reparse point — NOT the resolved final path.  Symlinks are
// decoded by std::filesystem::read_symlink; junctions (mount points) are
// not, so fall back to FSCTL_GET_REPARSE_POINT like cp's
// read_mount_point_target.
auto read_link_text_verbatim(const std::wstring& wpath)
    -> std::optional<std::wstring> {
  std::error_code ec;
  const auto target =
      std::filesystem::read_symlink(std::filesystem::path(wpath), ec);
  if (!ec) {
    return target.wstring();
  }
  UniqueHandle h(CreateFileW(
      wpath.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  if (!h) return std::nullopt;

  std::array<std::byte, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> buffer{};
  DWORD returned = 0;
  if (!DeviceIoControl(h.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0,
                        buffer.data(), static_cast<DWORD>(buffer.size()),
                        &returned, nullptr)) {
    return std::nullopt;
  }
  const auto* reparse =
      reinterpret_cast<const mv_win32_compat::ReparseDataBuffer*>(
          buffer.data());
  const wchar_t* path_buffer = nullptr;
  WORD print_offset = 0;
  WORD print_length = 0;
  WORD substitute_offset = 0;
  WORD substitute_length = 0;
  if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
    const auto& sl = reparse->SymbolicLinkReparseBuffer;
    path_buffer = sl.PathBuffer;
    print_offset = sl.PrintNameOffset;
    print_length = sl.PrintNameLength;
    substitute_offset = sl.SubstituteNameOffset;
    substitute_length = sl.SubstituteNameLength;
  } else if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
    const auto& mp = reparse->MountPointReparseBuffer;
    path_buffer = mp.PathBuffer;
    print_offset = mp.PrintNameOffset;
    print_length = mp.PrintNameLength;
    substitute_offset = mp.SubstituteNameOffset;
    substitute_length = mp.SubstituteNameLength;
  } else {
    return std::nullopt;
  }
  std::wstring_view print_name(path_buffer + print_offset / sizeof(wchar_t),
                               print_length / sizeof(wchar_t));
  if (print_name.empty()) {
    std::wstring_view substitute_name(
        path_buffer + substitute_offset / sizeof(wchar_t),
        substitute_length / sizeof(wchar_t));
    return strip_nt_prefix(substitute_name);
  }
  return strip_nt_prefix(print_name);
}

auto move_single_path(const std::string& src_path, const std::string& dest_path,
                      const CommandContext<MV_OPTIONS.size()>& ctx,
                      OverwriteMode overwrite_mode, UpdateMode update_mode)
    -> cp::Result<bool> {
  std::wstring wsrc_path = api_operand_w(src_path);
  std::wstring wdest_path = api_operand_w(dest_path);

  bool dest_exists =
      GetFileAttributesW(wdest_path.c_str()) != INVALID_FILE_ATTRIBUTES;
  if (overwrite_mode == OverwriteMode::no_clobber && dest_exists) {
    // [GNU] I_ALWAYS_NO skip: "not replacing %s" and the file fails
    // (copy.c:2438, 2425-2445); only --update=none skips silently
    // with success.
    safeErrorPrint("mv: not replacing '");
    safeErrorPrint(dest_path);
    safeErrorPrintLn("'");
    return false;
  }

  // --update mode handling
  if (update_mode == UpdateMode::none && dest_exists) {
    return true;
  }
  if (update_mode == UpdateMode::older && dest_exists) {
    // [GNU] The update skip never applies to directories
    // (copy.c:2353: `x->update && !S_ISDIR (src_mode)`).
    DWORD src_attrs = GetFileAttributesW(wsrc_path.c_str());
    const bool src_is_dir = src_attrs != INVALID_FILE_ATTRIBUTES &&
                            (src_attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!src_is_dir && !is_source_newer(wsrc_path, wdest_path)) {
      return true;
    }
  }

  // [GNU] With no -f/-i/-n, an unwritable destination is confirmed once
  // when stdin is a terminal; non-tty stdin proceeds without prompting.
  if (overwrite_mode == OverwriteMode::default_mode && dest_exists &&
      _isatty(_fileno(stdin))) {
    DWORD attrs = GetFileAttributesW(wdest_path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
      safeErrorPrint("mv: replace '");
      safeErrorPrint(dest_path);
      safeErrorPrint("', overriding mode 0444 (r--r--r--)? ");
      char response = '\0';
      std::cin.get(response);
      std::cin.ignore(1024, '\n');
      if (response != 'y' && response != 'Y') {
        return false;
      }
    }
  }

  if (overwrite_mode == OverwriteMode::interactive_always) {
    auto dest_exists = check_path_exists(dest_path);
    if (!dest_exists) {
      return std::unexpected(dest_exists.error());
    }
    if (*dest_exists) {
      auto confirmed = confirm_overwrite(dest_path);
      if (!confirmed) {
        return std::unexpected(confirmed.error());
      }
      if (!*confirmed) {
        return false;
      }
    }
  }

  // [GNU] Same-file guard (copy.c:2344-2350): renaming a file onto
  // itself must fail, not silently succeed through
  // MOVEFILE_REPLACE_EXISTING. Two names for one inode count as the
  // same file only for a single-link file or the same directory
  // entry (the same_name refinement).
  if (dest_exists && overwrite_mode != OverwriteMode::no_clobber) {
    auto identity_of = [](const std::wstring& path)
        -> std::optional<std::tuple<uint64_t, uint64_t, uint32_t>> {
      UniqueHandle handle(CreateFileW(
          path.c_str(), FILE_READ_ATTRIBUTES,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
          nullptr));
      if (!handle) return std::nullopt;
      BY_HANDLE_FILE_INFORMATION info{};
      if (!GetFileInformationByHandle(handle.get(), &info)) {
        return std::nullopt;
      }
      return std::tuple{static_cast<uint64_t>(info.dwVolumeSerialNumber),
                        (static_cast<uint64_t>(info.nFileIndexHigh) << 32) |
                            info.nFileIndexLow,
                        info.nNumberOfLinks};
    };
    auto src_id = identity_of(wsrc_path);
    auto dst_id = identity_of(wdest_path);
    if (src_id && dst_id && std::get<0>(*src_id) == std::get<0>(*dst_id) &&
        std::get<1>(*src_id) == std::get<1>(*dst_id)) {
      // Same inode: the same file when it has a single link, or when
      // both operands spell the same directory entry.
      const bool same_entry =
          std::get<2>(*src_id) <= 1 ||
          _wcsicmp(strip_nt_prefix(wsrc_path).c_str(),
                   strip_nt_prefix(wdest_path).c_str()) == 0;
      if (same_entry) {
        return std::unexpected("'" + src_path + "' and '" + dest_path +
                               "' are the same file");
      }
    }
  }

  auto backup_result =
      backup_existing_destination(wdest_path, dest_path, ctx);
  if (!backup_result) return std::unexpected(backup_result.error());
  const std::optional<std::string> made_backup = *backup_result;

  if (dest_exists) {
    DWORD attrs = GetFileAttributesW(wdest_path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY))
      SetFileAttributesW(wdest_path.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
  }

  // Try to rename first
  bool used_copy_fallback = false;
  if (!MoveFileExW(wsrc_path.c_str(), wdest_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    used_copy_fallback = true;
    if (ctx.has("--no-copy")) {
      return std::unexpected(
          "rename failed and copying is disabled by --no-copy");
    }

    // If rename fails, try copy and delete
    // First, check if source is a file
    DWORD src_attr = GetFileAttributesW(wsrc_path.c_str());
    if (src_attr == INVALID_FILE_ATTRIBUTES) {
      return std::unexpected("cannot access '" + src_path +
                             "': No such file or directory");
    }

    // [GNU] Check if source is a reparse point (symlink/junction)
    // and copy it as a symlink instead of following it
    if ((src_attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      // [GNU] #1365: recreate the link at the destination with its text
      // preserved verbatim.  The old code read the RESOLVED final path via
      // GetFinalPathNameByHandleW (an absolute \\?\ path, not the link
      // text): creation with it failed, and the silent CopyFileW fallback
      // degraded the move into a regular-file copy of the target's
      // contents while still reporting success.
      const auto link_text = read_link_text_verbatim(wsrc_path);
      if (!link_text) {
        return std::unexpected("cannot read symlink target of '" + src_path +
                               "'");
      }
      const bool is_dir = (src_attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
      DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
      if (is_dir) flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
      if (!CreateSymbolicLinkW(
              wdest_path.c_str(),
              win32_normalize_symlink_target(*link_text).c_str(), flags)) {
        // Never degrade into a content copy: a failed recreation is an
        // error, like GNU mv failing to recreate the link.
        return std::unexpected("cannot create symbolic link '" + dest_path +
                               "': " +
                               win32_posix_error_text(GetLastError()));
      }
      // Remove the source link itself.  A directory link needs
      // RemoveDirectoryW; DeleteFileW alone cannot remove it, which used
      // to leave the source behind after a "successful" move.
      const bool removed =
          is_dir ? (RemoveDirectoryW(wsrc_path.c_str()) ||
                    DeleteFileW(wsrc_path.c_str()))
                 : (DeleteFileW(wsrc_path.c_str()) ||
                    RemoveDirectoryW(wsrc_path.c_str()));
      if (!removed) {
        return std::unexpected("cannot remove source link '" + src_path +
                               "'");
      }
    } else {
      // [GNU] MoveFileExW cannot rename across volumes (and some tools hold
      // handles that block same-volume renames); fall back to a recursive
      // copy of files *and* directories, then remove the source only after
      // the copy succeeds (mv.c do_move).  Directories used to fall through
      // this branch silently, reporting success without moving anything.
      if ((src_attr & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
          path_is_at_or_below(wsrc_path, wdest_path)) {
        // [GNU] copy.c:2091: "cannot copy a directory, %s, into itself, %s"
        return std::unexpected("cannot copy a directory, '" + src_path +
                               "', into itself, '" + dest_path + "'");
      }
      std::error_code ec;
      // [GNU] The copy fallback preserves links (mv.c:135 sets
      // preserve_links for the fallback): copy_symlinks keeps nested
      // symlinks as links instead of duplicating their targets'
      // contents. (Hard-link grouping inside the tree is not yet
      // preserved.)
      std::filesystem::copy(
          std::filesystem::path(wsrc_path), std::filesystem::path(wdest_path),
          std::filesystem::copy_options::recursive |
              std::filesystem::copy_options::overwrite_existing |
              std::filesystem::copy_options::copy_symlinks,
          ec);
      if (ec) {
        return std::unexpected("cannot copy '" + src_path + "' to '" +
                               dest_path + "': " + ec.message());
      }
      std::filesystem::remove_all(std::filesystem::path(wsrc_path), ec);
      if (ec) {
        return std::unexpected("cannot remove '" + src_path +
                               "': " + ec.message());
      }
    }
  }

  bool verbose = ctx.get<bool>("--verbose", false) ||
                 ctx.get<bool>("-v", false) || ctx.get<bool>("--debug", false);
  if (verbose) {
    // [GNU] A rename reports "renamed '<src>' -> '<dest>'"; the copy
    // fallback reports "copied '<src>' -> '<dest>'" and then
    // "removed '<src>'" (mv.c do_move / copy.c verbose).  The backup
    // note rides the first line (copy.c:2085-2088).
    safePrint(used_copy_fallback ? "copied '" : "renamed '");
    safePrint(src_path);
    safePrint("' -> '");
    safePrint(dest_path);
    safePrint("'");
    if (made_backup) {
      safePrint(" (backup: '");
      safePrint(*made_backup);
      safePrint("')");
    }
    safePrint("\n");
    if (used_copy_fallback) {
      safePrint("removed '");
      safePrint(src_path);
      safePrint("'\n");
    }
  }

  return true;
}

auto process_single_source(const std::string& src_path,
                           const MoveContext& move_ctx, bool dest_is_dir,
                           const CommandContext<MV_OPTIONS.size()>& ctx,
                           OverwriteMode overwrite_mode) -> cp::Result<bool> {
  // [GNU] A trailing separator forces a directory operand: a regular file
  // fails at stat time, while a symlink/junction passes stat but the rename
  // fails ENOTDIR (uutils#10026).
  bool trailing_sep = src_path.size() > 1 &&
                      (src_path.back() == '/' || src_path.back() == '\\');
  if (trailing_sep) {
    std::string stripped = strip_trailing_slashes(src_path);
    DWORD attrs = GetFileAttributesW(api_operand_w(stripped).c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      return std::unexpected("cannot stat '" + src_path +
                             "': No such file or directory");
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      return std::unexpected("cannot stat '" + src_path + "': Not a directory");
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
      std::string final_dest = move_ctx.dest_path;
      if (dest_is_dir) {
        std::wstring wsrc = utf8_to_wstring(stripped);
        final_dest += "\\" + wstring_to_utf8(PathFindFileNameW(wsrc.data()));
      }
      return std::unexpected("cannot move '" + src_path + "' to '" +
                             final_dest + "': Not a directory");
    }
    // A real directory keeps moving normally.
  }

  auto src_exists = check_path_exists(src_path);
  if (!src_exists) {
    return std::unexpected(src_exists.error());
  }
  if (!*src_exists) {
    return std::unexpected("cannot stat '" + src_path +
                           "': No such file or directory");
  }

  auto final_dest = build_dest_path(src_path, move_ctx.dest_path, dest_is_dir);
  if (!final_dest) {
    return std::unexpected(final_dest.error());
  }

  return move_single_path(src_path, *final_dest, ctx, overwrite_mode,
                          move_ctx.update_mode);
}

// Emulate renameat2(RENAME_EXCHANGE): Windows has no atomic swap, so do a
// best-effort three-rename exchange (src -> tmp, dest -> src, tmp -> dest).
// Both operands must exist and live on the same volume, matching GNU's
// constraint that the exchange is a same-filesystem rename operation.
auto exchange_paths(const std::string& src_path, const std::string& dest_path,
                    bool verbose) -> cp::Result<bool> {
  // .normalized (not .extended): GetVolumePathNameW rejects \\?\ paths, and
  // the operands here are already absolute after MSYS-to-drive conversion.
  std::wstring wsrc = native_path::make_api_path_operand(src_path).normalized;
  std::wstring wdest = native_path::make_api_path_operand(dest_path).normalized;
  DWORD src_attr = GetFileAttributesW(wsrc.c_str());
  if (src_attr == INVALID_FILE_ATTRIBUTES) {
    return std::unexpected("cannot stat '" + src_path +
                           "': No such file or directory");
  }
  DWORD dest_attr = GetFileAttributesW(wdest.c_str());
  if (dest_attr == INVALID_FILE_ATTRIBUTES) {
    return std::unexpected("cannot stat '" + dest_path +
                           "': No such file or directory");
  }
  // RENAME_EXCHANGE requires same filesystem; cheap check: same volume root.
  wchar_t src_root[MAX_PATH] = {};
  wchar_t dest_root[MAX_PATH] = {};
  if (GetVolumePathNameW(wsrc.c_str(), src_root, MAX_PATH) &&
      GetVolumePathNameW(wdest.c_str(), dest_root, MAX_PATH) &&
      _wcsicmp(src_root, dest_root) != 0) {
    return std::unexpected("cannot exchange '" + src_path + "' and '" +
                           dest_path + "': different volumes");
  }
  std::filesystem::path tmp =
      std::filesystem::path(wsrc).parent_path() /
      (".mv-exchange-" + std::to_string(GetCurrentProcessId()) + "-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::wstring wtmp = tmp.wstring();
  auto rollback = [&]() {
    // Best effort: restore whichever step already completed.
    if (GetFileAttributesW(wtmp.c_str()) != INVALID_FILE_ATTRIBUTES) {
      MoveFileExW(wtmp.c_str(), wsrc.c_str(), 0);
    }
  };
  if (!MoveFileExW(wsrc.c_str(), wtmp.c_str(), 0)) {
    return std::unexpected("cannot move '" + src_path +
                           "': rename to temporary failed");
  }
  if (!MoveFileExW(wdest.c_str(), wsrc.c_str(), 0)) {
    rollback();
    return std::unexpected("cannot move '" + dest_path + "' to '" + src_path +
                           "'");
  }
  if (!MoveFileExW(wtmp.c_str(), wdest.c_str(), 0)) {
    // Try to put dest back before restoring src.
    MoveFileExW(wsrc.c_str(), wdest.c_str(), 0);
    rollback();
    return std::unexpected("cannot move temporary to '" + dest_path + "'");
  }
  if (verbose) {
    safePrint("'");
    safePrint(src_path);
    safePrint("' <-> '");
    safePrint(dest_path);
    safePrint("'\n");
  }
  return true;
}

template <size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<bool> {
  bool want_exchange = ctx.has("--exchange");

  return parse_arguments(ctx).and_then(
      [&, want_exchange](MoveContext move_ctx) -> cp::Result<bool> {
        auto overwrite_mode = parse_overwrite_mode(ctx);
        if (!overwrite_mode) {
          return std::unexpected(overwrite_mode.error());
        }

        if (want_exchange) {
          if (move_ctx.source_paths.size() != 1) {
            return std::unexpected(
                "--exchange requires exactly one source and one destination");
          }
          bool verbose = ctx.get<bool>("--verbose", false) ||
                         ctx.get<bool>("-v", false) ||
                         ctx.get<bool>("--debug", false);
          return exchange_paths(move_ctx.source_paths[0], move_ctx.dest_path,
                                verbose);
        }

        // [GNU] --backup and --no-clobber are mutually exclusive
        // (mv.c:512-516); the check runs before anything is moved.
        {
          auto control = mv_make_backups(ctx);
          if (!control) {
            return std::unexpected(control.error());
          }
          // The conflict keys on the backup options being GIVEN, not
          // on the engaged control: GNU answers the mutual-exclusion
          // error for --backup=none -n as well (mv.c:512-515 with
          // make_backups tracking option givenness), while a bare -n
          // must fall through to the 'not replacing' skip.  The
          // mv_make_backups call above still runs first so an invalid
          // --backup value reports its argmatch error ahead of this.
          if (mv_backup_option_given(ctx) &&
              *overwrite_mode == OverwriteMode::no_clobber) {
            return std::unexpected(
                "options --backup and --no-clobber are mutually exclusive\n"
                "Try 'mv --help' for more information.");
          }
        }

        auto dest_exists = check_path_exists(move_ctx.dest_path);
        if (!dest_exists) {
          return std::unexpected(dest_exists.error());
        }
        bool dest_is_dir = false;
        if (*dest_exists) {
          auto is_dir = check_is_directory(move_ctx.dest_path);
          if (!is_dir) {
            return std::unexpected(is_dir.error());
          }
          dest_is_dir = *is_dir && !move_ctx.no_target_directory;
        }
        if ((move_ctx.target_directory_option ||
             move_ctx.source_paths.size() > 1) &&
            !move_ctx.no_target_directory && !dest_is_dir) {
          // [GNU] The diagnostic names the target and the reason:
          // "target directory %s" for -t (mv.c:458-462), "target %s"
          // for the multi-operand form (mv.c:492-496), each with the
          // errno text.
          const std::string reason = *dest_exists
                                         ? "Not a directory"
                                         : "No such file or directory";
          return std::unexpected(
              std::string(move_ctx.target_directory_option
                              ? "target directory '"
                              : "target '") +
              move_ctx.dest_path + "': " + reason);
        }

        // [GNU] A trailing separator makes DEST a directory operand
        // (lib/targetdir.c target_directory_operand opens it O_DIRECTORY):
        // when it names a missing path or a regular file the move fails
        // before any rename or copy attempt — "failed to access 'DEST': Not
        // a directory" — instead of silently creating the stripped name as
        // a regular file.  This keeps the copy fallback from bypassing the
        // #1052 trailing-separator invariant (std::filesystem::copy strips
        // the separator and creates a file).
        const bool dest_trailing_sep =
            move_ctx.dest_path.size() > 1 &&
            (move_ctx.dest_path.back() == '/' ||
             move_ctx.dest_path.back() == '\\');
        if (dest_trailing_sep && !dest_is_dir) {
          return std::unexpected("failed to access '" + move_ctx.dest_path +
                                 "': Not a directory");
        }

        // --interactive=once: prompt once before removing more than three
        // files, or when moving recursively
        bool recursive =
            ctx.get<bool>("-r", false) || ctx.get<bool>("--recursive", false);
        bool need_prompt =
            (*overwrite_mode == OverwriteMode::interactive_once &&
             (move_ctx.source_paths.size() > 3 || recursive));
        if (need_prompt) {
          safeErrorPrint("mv: remove ");
          if (recursive) {
            safeErrorPrint("directory ");
            safeErrorPrint(move_ctx.source_paths[0]);
          } else {
            safeErrorPrint(std::to_string(move_ctx.source_paths.size()));
            safeErrorPrint(" arguments");
          }
          safeErrorPrint("? ");
          char response = '\0';
          std::cin >> response;
          if (response != 'y' && response != 'Y') {
            return true;
          }
        }

        bool success = true;
        for (const auto& src_path : move_ctx.source_paths) {
          auto result = process_single_source(src_path, move_ctx, dest_is_dir,
                                              ctx, *overwrite_mode);
          if (!result) {
            // [GNU] mv.c continues with the remaining sources after a
            // failure and reports a nonzero exit at the end.
            cp::report_custom_error(L"mv", utf8_to_wstring(result.error()));
            success = false;
            continue;
          }
          if (!*result) {
            success = false;
          }
        }
        return success;
      });
}

}  // namespace mv_pipeline

REGISTER_COMMAND(
    mv,
    /* cmd_name */ "mv",
    /* cmd_synopsis */ "move (rename) files",
    /* cmd_desc */
    "Rename SOURCE to DEST, or move SOURCE(s) to DIRECTORY.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options "
    "too.\n",
    /* examples */
    "  mv file1 file2            Rename file1 to file2\n"
    "  mv file1 file2 dir        Move file1 and file2 to directory dir\n"
    "  mv -i file1 file2         Prompt before overwriting file2\n"
    "  mv -v file1 file2         Verbose output\n",
    /* see_also */ "cp(1), rm(1), ln(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    MV_OPTIONS) {
  using namespace mv_pipeline;
  using namespace core::pipeline;

  auto result = process_command(ctx);
  if (!result) {
    report_error(result, L"mv");
    // [GNU] operand-count errors are followed by the try-help hint.
    if (result.error().starts_with("missing ")) {
      safeErrorPrintLn(winux::i18n::format(
          "common.try_help", "Try '{} --help' for more information.", "mv"));
    }
    return 1;
  }

  return *result ? 0 : 1;
}
