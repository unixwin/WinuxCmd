/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: mv.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
 * - @a -b: Like --backup but does not accept an argument [TODO]
 * - @a -f, @a --force: Do not prompt before overwriting [IMPLEMENTED]
 * - @a -i: Prompt before overwrite [IMPLEMENTED]
 * - @a -n, @a --no-clobber: Do not overwrite an existing file [IMPLEMENTED]
 * - @a --strip-trailing-slashes: Remove any trailing slashes from each SOURCE
 * argument [TODO]
 * - @a -S, @a --suffix: Override the usual backup suffix [TODO]
 * - @a -t, @a --target-directory: Move all SOURCE arguments into DIRECTORY
 * [IMPLEMENTED]
 * - @a -T, @a --no-target-directory: Treat DEST as a normal file [IMPLEMENTED]
 * - @a -u: Move only when the SOURCE file is newer than the destination file or
 * when the destination file is missing [TODO]
 * - @a -v, @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a -Z, @a --context: Set SELinux security context of destination file to
 * default type [TODO]
 * - @a --backup: Make a backup of each existing destination file [TODO]
 * - @a --force: Do not prompt before overwriting [IMPLEMENTED]
 * - @a --interactive: Prompt according to WHEN: never, once (-I), or always
 * (-i) [IMPLEMENTED]
 * - @a --no-clobber: Do not overwrite an existing file [IMPLEMENTED]
 * - @a --suffix: Override the usual backup suffix [TODO]
 * - @a --target-directory: Move all SOURCE arguments into DIRECTORY
 * [IMPLEMENTED]
 * - @a --no-target-directory: Treat DEST as a normal file [IMPLEMENTED]
 * - @a --update: Move only when the SOURCE file is newer than the destination
 * file or when the destination file is missing [TODO]
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
    std::array{OPTION("-b", "", "like --backup but does not accept an argument"),
               OPTION("-f", "--force", "do not prompt before overwriting"),
               OPTION("-i", "", "prompt before overwrite"),
               OPTION("-n", "--no-clobber", "do not overwrite an existing file"),
               OPTION("", "--strip-trailing-slashes", "remove any trailing slashes from each SOURCE argument"),
               OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
               OPTION("-t", "--target-directory", "move all SOURCE arguments into DIRECTORY", STRING_TYPE),
               OPTION("-T", "--no-target-directory", "treat DEST as a normal file"),
               OPTION("-u", "--update", "move only when the SOURCE file is newer than the destination file or when the destination file is missing"),
               OPTION("-v", "--verbose", "explain what is being done"),
               OPTION("-Z", "--context", "set SELinux security context of destination file to default type"),
               OPTION("", "--backup", "make a backup of each existing destination file"),
               OPTION("", "--force", "do not prompt before overwriting"),
               OPTION("", "--interactive", "prompt according to WHEN: never, once (-I), or always (-i)"),
               OPTION("", "--no-clobber", "do not overwrite an existing file"),
               OPTION("", "--suffix", "override the usual backup suffix", STRING_TYPE),
               OPTION("", "--target-directory", "move all SOURCE arguments into DIRECTORY", STRING_TYPE),
               OPTION("", "--no-target-directory", "treat DEST as a normal file"),
               OPTION("", "--update", "move only when the SOURCE file is newer than the destination file or when the destination file is missing"),
               OPTION("", "--verbose", "explain what is being done"),
               OPTION("", "--context", "set SELinux security context of destination file to default type")};
// clang-format on

// ======================================================
// Pipeline components
// ======================================================
namespace mv_pipeline {
namespace cp = core::pipeline;

struct MoveContext {
  SmallVector<std::string, 64> source_paths;
  std::string dest_path;
};

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

  bool strip_slashes = ctx.get<bool>("--strip-trailing-slashes", false);
  if (!target_dir.empty()) {
    move_ctx.dest_path = target_dir;
    for (auto arg : ctx.positionals) {
      auto src = std::string(arg);
      if (strip_slashes) src = strip_trailing_slashes(std::move(src));
      move_ctx.source_paths.push_back(std::move(src));
    }
  } else {
    // Regular case: last argument is destination
    if (ctx.positionals.size() < 2) {
      return std::unexpected("missing file operand");
    }

    for (size_t i = 0; i < ctx.positionals.size() - 1; ++i) {
      auto src = std::string(ctx.positionals[i]);
      if (strip_slashes) src = strip_trailing_slashes(std::move(src));
      move_ctx.source_paths.push_back(std::move(src));
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

  // Fallback to dynamic allocation if needed
  std::string file_name_str(file_name_length, 0);
  WideCharToMultiByte(CP_UTF8, 0, file_name, -1, &file_name_str[0],
                      file_name_length, NULL, NULL);
  return dest_path + "\\" + file_name_str;
}

auto confirm_overwrite(const std::string& dest_path) -> cp::Result<bool> {
  // OPTIMIZED: Avoid wstring concatenation
  safeErrorPrint("mv: overwrite '");
  safeErrorPrint(dest_path);
  safeErrorPrint("'? (y/n) ");
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
  bool backup = ctx.get<bool>("-b", false) || ctx.get<bool>("--backup", false);
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
                      const CommandContext<MV_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  std::wstring wsrc_path = utf8_to_wstring(src_path);
  std::wstring wdest_path = utf8_to_wstring(dest_path);

  bool dest_exists = GetFileAttributesW(wdest_path.c_str()) !=
                     INVALID_FILE_ATTRIBUTES;
  bool no_clobber =
      ctx.get<bool>("--no-clobber", false) || ctx.get<bool>("-n", false);
  if (no_clobber && dest_exists) {
    return true;
  }

  bool update = ctx.get<bool>("--update", false) || ctx.get<bool>("-u", false);
  if (update && dest_exists && !is_source_newer(wsrc_path, wdest_path)) {
    return true;
  }

  bool interactive =
      ctx.get<bool>("--interactive", false) || ctx.get<bool>("-i", false);
  if (interactive) {
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

  // Try to rename first
  if (!MoveFileExW(wsrc_path.c_str(), wdest_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    // If rename fails, try copy and delete
    // First, check if source is a file
    DWORD src_attr = GetFileAttributesW(wsrc_path.c_str());
    if (src_attr == INVALID_FILE_ATTRIBUTES) {
      return std::unexpected("cannot access '" + src_path +
                             "': No such file or directory");
    }

    if (!(src_attr & FILE_ATTRIBUTE_DIRECTORY)) {
      // It's a file, try to copy
      if (!CopyFileW(wsrc_path.c_str(), wdest_path.c_str(), FALSE)) {
        return std::unexpected("cannot copy '" + src_path + "' to '" +
                               dest_path + "'");
      }
      // If copy succeeds, delete the source
      if (!DeleteFileW(wsrc_path.c_str())) {
        return std::unexpected("cannot delete source file '" + src_path + "'");
      }
    } else {
      // It's a directory, rename failed (maybe cross-volume)
      return std::unexpected("cannot move directory '" + src_path + "' to '" +
                             dest_path + "': cross-volume move not supported");
    }
  }

  bool verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
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
                           const CommandContext<MV_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
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

  return move_single_path(src_path, *final_dest, ctx);
}

template <size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<bool> {
  return parse_arguments(ctx).and_then(
      [&](MoveContext move_ctx) -> cp::Result<bool> {
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
          dest_is_dir = *is_dir;
        }
        bool success = true;
        for (const auto& src_path : move_ctx.source_paths) {
          auto result =
              process_single_source(src_path, move_ctx, dest_is_dir, ctx);
          if (!result) {
            return std::unexpected(result.error());
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
    return 1;
  }

  return *result ? 0 : 1;
}
