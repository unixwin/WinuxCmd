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
 * - @a --interactive: Prompt according to WHEN: never, once (-I), or always
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
               // [GNU]
               OPTION("-I", "", "prompt once before removing more than three files, or when moving recursively"),
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
               OPTION("", "--interactive", "prompt according to WHEN: never, once (-I), or always (-i)", OPTIONAL_STRING_TYPE),
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

  for (size_t i = 0; i < ctx.raw_args.size(); ++i) {
    std::string arg(ctx.raw_args[i]);
    if (arg == "--") break;

    if (arg == "--force") {
      mode = OverwriteMode::force;
      continue;
    }
    if (arg == "--no-clobber") {
      mode = OverwriteMode::no_clobber;
      continue;
    }
    if (arg == "--interactive") {
      mode = OverwriteMode::interactive_always;
      continue;
    }
    if (arg.rfind("--interactive=", 0) == 0) {
      auto when = arg.substr(std::string("--interactive=").size());
      if (when == "never") {
        mode = OverwriteMode::force;
      } else if (when == "once") {
        mode = OverwriteMode::interactive_once;
      } else if (when == "always") {
        mode = OverwriteMode::interactive_always;
      } else {
        return std::unexpected("invalid argument '" + when +
                               "' for '--interactive'");
      }
      continue;
    }

    if (arg.size() < 2 || arg[0] != '-' || arg[1] == '-') continue;
    for (size_t j = 1; j < arg.size(); ++j) {
      switch (arg[j]) {
        case 'f':
          mode = OverwriteMode::force;
          break;
        case 'i':
          mode = OverwriteMode::interactive_always;
          break;
        case 'I':
          mode = OverwriteMode::interactive_once;
          break;
        case 'n':
          mode = OverwriteMode::no_clobber;
          break;
        default:
          break;
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

auto parse_arguments(const CommandContext<MV_OPTIONS.size()>& ctx)
    -> cp::Result<MoveContext> {
  MoveContext move_ctx;

  // Get target directory if specified
  std::string target_dir = ctx.get<std::string>("--target-directory", "");
  if (target_dir.empty()) {
    target_dir = ctx.get<std::string>("-t", "");
  }
  move_ctx.no_target_directory = ctx.get<bool>("-T", false) ||
                                 ctx.get<bool>("--no-target-directory", false);
  if (!target_dir.empty() && move_ctx.no_target_directory) {
    return std::unexpected(
        "cannot combine --target-directory and --no-target-directory");
  }

  // Parse --update[=UPDATE] mode
  if (ctx.get<bool>("-u", false)) {
    move_ctx.update_mode = UpdateMode::older;
  } else if (ctx.has("--update")) {
    auto update_opt = ctx.get<std::string>("--update", "");
    if (update_opt == "all") {
      move_ctx.update_mode = UpdateMode::all;
    } else if (update_opt == "none") {
      move_ctx.update_mode = UpdateMode::none;
    } else if (update_opt == "older" || update_opt.empty()) {
      move_ctx.update_mode = UpdateMode::older;
    } else {
      return std::unexpected("invalid argument '" + update_opt +
                             "' for '--update'");
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
    if (ctx.positionals.size() < 2) {
      return std::unexpected("missing file operand");
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

auto check_path_exists(const std::string& path) -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);
  DWORD attr = GetFileAttributesW(wpath.c_str());
  return attr != INVALID_FILE_ATTRIBUTES;
}

auto check_is_directory(const std::string& path) -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);
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

auto backup_existing_destination(const std::wstring& dest_path,
                                 const CommandContext<MV_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  bool backup = ctx.get<bool>("-b", false) || ctx.has("--backup");
  if (!backup) return true;
  if (GetFileAttributesW(dest_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return true;
  }

  std::string suffix = ctx.get<std::string>("--suffix", "");
  if (suffix.empty()) suffix = ctx.get<std::string>("-S", "");
  if (suffix.empty()) suffix = "~";

  std::wstring backup_path = dest_path + utf8_to_wstring(suffix);
  if (!MoveFileExW(dest_path.c_str(), backup_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    return std::unexpected("cannot create backup for destination");
  }
  return true;
}

auto move_single_path(const std::string& src_path, const std::string& dest_path,
                      const CommandContext<MV_OPTIONS.size()>& ctx,
                      OverwriteMode overwrite_mode, UpdateMode update_mode)
    -> cp::Result<bool> {
  std::wstring wsrc_path = utf8_to_wstring(src_path);
  std::wstring wdest_path = utf8_to_wstring(dest_path);

  bool dest_exists =
      GetFileAttributesW(wdest_path.c_str()) != INVALID_FILE_ATTRIBUTES;
  if (overwrite_mode == OverwriteMode::no_clobber && dest_exists) {
    return true;
  }

  // --update mode handling
  if (update_mode == UpdateMode::none && dest_exists) {
    return true;
  }
  if (update_mode == UpdateMode::older && dest_exists &&
      !is_source_newer(wsrc_path, wdest_path)) {
    return true;
  }

  // [GNU] With no -f/-i/-n/-I, an unwritable destination is confirmed once
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
        return true;
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
        return true;
      }
    }
  }

  auto backup_result = backup_existing_destination(wdest_path, ctx);
  if (!backup_result) return backup_result;

  if (dest_exists) {
    DWORD attrs = GetFileAttributesW(wdest_path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY))
      SetFileAttributesW(wdest_path.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
  }

  // Try to rename first
  if (!MoveFileExW(wsrc_path.c_str(), wdest_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
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
      // Get the symlink target
      wchar_t target[4096] = {};
      if (!GetFinalPathNameByHandleW(
              CreateFileW(wsrc_path.c_str(), 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr),
              target, 4096, 0)) {
        return std::unexpected("cannot read symlink target");
      }
      // Create symlink at destination
      bool is_dir = (src_attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
      if (!CreateSymbolicLinkW(wdest_path.c_str(), target, is_dir ? 1 : 0)) {
        // Fallback: copy the file as-is (will follow symlink)
        if (!CopyFileW(wsrc_path.c_str(), wdest_path.c_str(), FALSE)) {
          return std::unexpected("cannot copy '" + src_path + "' to '" +
                                 dest_path + "'");
        }
      }
      // If copy/symlink succeeds, delete the source
      if (!DeleteFileW(wsrc_path.c_str())) {
        return std::unexpected("cannot delete source file '" + src_path + "'");
      }
    } else if (!(src_attr & FILE_ATTRIBUTE_DIRECTORY)) {
      // MoveFileEx cannot rename a directory across volumes.  Fall back to a
      // recursive copy, then remove the source only after the copy succeeds.
      std::error_code ec;
      std::filesystem::copy(
          std::filesystem::path(wsrc_path), std::filesystem::path(wdest_path),
          std::filesystem::copy_options::recursive |
              std::filesystem::copy_options::overwrite_existing,
          ec);
      if (ec) {
        return std::unexpected("cannot copy directory '" + src_path + "' to '" +
                               dest_path + "': " + ec.message());
      }
      std::filesystem::remove_all(std::filesystem::path(wsrc_path), ec);
      if (ec) {
        return std::unexpected("cannot delete source directory '" + src_path +
                               "': " + ec.message());
      }
    }
  }

  bool verbose = ctx.get<bool>("--verbose", false) ||
                 ctx.get<bool>("-v", false) || ctx.get<bool>("--debug", false);
  if (verbose) {
    // OPTIMIZED: Avoid multiple wstring conversions and concatenations
    safePrint("'");
    safePrint(src_path);
    safePrint("' -> '");
    safePrint(dest_path);
    safePrint("'\n");
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
    DWORD attrs = GetFileAttributesW(utf8_to_wstring(stripped).c_str());
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
  std::wstring wsrc = utf8_to_wstring(src_path);
  std::wstring wdest = utf8_to_wstring(dest_path);
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
            !dest_is_dir) {
          return std::unexpected("target is not a directory");
        }

        // -I / --interactive=once: prompt once before removing more than three
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
