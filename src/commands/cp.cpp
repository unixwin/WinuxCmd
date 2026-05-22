/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: cp.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for cp.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#pragma comment(lib, "shlwapi.lib")
#include "core/command_macros.h"

import std;
import core;
import utils;
/**
 * @brief CP command options definition
 *
 * This array defines all the options supported by the cp command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -a, @a --archive: Same as -dR --preserve=all [TODO]
 * - @a -b: Like --backup but does not accept an argument [IMPLEMENTED]
 * - @a -d: Same as --no-dereference --preserve=links [TODO]
 * - @a -f, @a --force: If an existing destination file cannot be opened, remove
 * it and try again [TODO]
 * - @a -i, @a --interactive: Prompt before overwrite [IMPLEMENTED]
 * - @a -H: Follow command-line symbolic links in SOURCE [TODO]
 * - @a -l, @a --link: Hard link files instead of copying [TODO]
 * - @a -L, @a --dereference: Always follow symbolic links in SOURCE [TODO]
 * - @a -n, @a --no-clobber: Do not overwrite an existing file and do not fail
 * [TODO]
 * - @a -P, @a --no-dereference: Never follow symbolic links in SOURCE [TODO]
 * - @a -p: Same as --preserve=mode,ownership,timestamps [TODO]
 * - @a -R, @a --recursive: Copy directories recursively [IMPLEMENTED]
 * - @a -r, @a --recursive: Copy directories recursively [IMPLEMENTED]
 * - @a -s, @a --symbolic-link: Make symbolic links instead of copying [TODO]
 * - @a -S, @a --suffix: Override the usual backup suffix [IMPLEMENTED]
 * - @a -t, @a --target-directory: Copy all SOURCE arguments into DIRECTORY
 * [IMPLEMENTED]
 * - @a -T, @a --no-target-directory: Treat DEST as a normal file [TODO]
 * - @a -u: Equivalent to --update[=older] [TODO]
 * - @a -v, @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a -x, @a --one-file-system: Stay on this file system [TODO]
 * - @a -Z: Set SELinux security context of destination file to default type
 * [TODO]
 */

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Constants
// ======================================================
namespace cp_constants {
constexpr char DEFAULT_BACKUP_SUFFIX[] = "~";
}

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr CP_OPTIONS = std::array{
    OPTION("-a", "--archive", "same as -dR --preserve=all"),
    OPTION("-b", "", "like --backup but does not accept an argument"),
    OPTION("", "--backup", "make a backup of each existing destination file",
           OPTIONAL_STRING_TYPE),
    OPTION("-d", "", "same as --no-dereference --preserve=links"),
    OPTION("-f", "--force",
           "if an existing destination file cannot be opened, remove it and "
           "try again"),
    OPTION("-i", "--interactive", "prompt before overwrite"),
    OPTION("-H", "", "follow command-line symbolic links in SOURCE"),
    OPTION("-l", "--link", "hard link files instead of copying"),
    OPTION("-L", "--dereference", "always follow symbolic links in SOURCE"),
    OPTION("-n", "--no-clobber",
           "do not overwrite an existing file and do not fail"),
    OPTION("-P", "--no-dereference", "never follow symbolic links in SOURCE"),
    OPTION("-p", "", "same as --preserve=mode,ownership,timestamps"),
    OPTION("-R", "--recursive", "copy directories recursively"),
    OPTION("-r", "--recursive", "copy directories recursively"),
    OPTION("-s", "--symbolic-link", "make symbolic links instead of copying"),
    OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
    OPTION("-t", "--target-directory",
           "copy all SOURCE arguments into DIRECTORY", STRING_TYPE),
    OPTION("-T", "--no-target-directory", "treat DEST as a normal file"),
    OPTION("-u", "--update", "equivalent to --update[=older]"),
    OPTION("-v", "--verbose", "explain what is being done"),
    OPTION("-x", "--one-file-system", "stay on this file system"),
    OPTION("-Z", "",
           "set SELinux security context of destination file to default type")};

// ======================================================
// Pipeline components
// ======================================================
namespace cp_pipeline {
namespace cp = core::pipeline;

auto append_expanded_source(std::vector<std::string>& source_paths,
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

// ----------------------------------------------
// 1. Validate arguments
// ----------------------------------------------
auto validate_arguments(const CommandContext<CP_OPTIONS.size()>& ctx)
    -> cp::Result<std::pair<std::vector<std::string>, std::string>> {
  std::vector<std::string> sourcePaths;
  std::string destPath;

  // Get target directory if specified
  std::string target_dir = ctx.get<std::string>("--target-directory", "");
  if (target_dir.empty()) {
    target_dir = ctx.get<std::string>("-t", "");
  }
  if (!target_dir.empty()) {
    bool no_target_directory = ctx.get<bool>("-T", false) ||
                               ctx.get<bool>("--no-target-directory", false);
    if (no_target_directory) {
      return std::unexpected(
          "cannot combine --target-directory and --no-target-directory");
    }

    DWORD attr = GetFileAttributesW(utf8_to_wstring(target_dir).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES ||
        !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
      return std::unexpected("target is not a directory");
    }

    destPath = target_dir;
    for (auto arg : ctx.positionals) {
      append_expanded_source(sourcePaths, arg);
    }
  } else {
    // Regular case: last argument is destination
    if (ctx.positionals.size() < 2) {
      return std::unexpected("missing file operand");
    }

    for (size_t i = 0; i < ctx.positionals.size() - 1; ++i) {
      append_expanded_source(sourcePaths, ctx.positionals[i]);
    }
    destPath = std::string(ctx.positionals.back());
  }

  if (sourcePaths.empty()) {
    return std::unexpected("missing file operand");
  }

  return std::pair{sourcePaths, destPath};
}

// ----------------------------------------------
// 2. Check if destination is directory
// ----------------------------------------------
auto check_destination(
    const std::pair<std::vector<std::string>, std::string>& paths,
    bool no_target_directory)
    -> cp::Result<std::tuple<std::vector<std::string>, std::string, bool>> {
  const auto& [sourcePaths, destPath] = paths;

  std::wstring wdestPath = utf8_to_wstring(destPath);
  DWORD attr = GetFileAttributesW(wdestPath.c_str());
  bool destIsDir = !no_target_directory && (attr != INVALID_FILE_ATTRIBUTES) &&
                   (attr & FILE_ATTRIBUTE_DIRECTORY);

  if (sourcePaths.size() > 1 && !destIsDir) {
    return std::unexpected("target is not a directory");
  }

  return std::tuple{sourcePaths, destPath, destIsDir};
}

// ----------------------------------------------
// 3. Create directory recursively
// ----------------------------------------------
auto create_directory_recursive(const std::string& path) -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);
  if (CreateDirectoryW(wpath.c_str(), NULL) ||
      GetLastError() == ERROR_ALREADY_EXISTS) {
    return true;
  }

  // If parent directory doesn't exist, create it first
  size_t lastSlash = path.find_last_of('\\');
  if (lastSlash == std::string::npos) {
    return std::unexpected("cannot create directory");
  }

  std::string parentPath = path.substr(0, lastSlash);
  auto parentResult = create_directory_recursive(parentPath);
  if (!parentResult) {
    return parentResult;
  }

  // Now create the current directory
  if (CreateDirectoryW(wpath.c_str(), NULL) == 0) {
    return std::unexpected("cannot create directory");
  }

  return true;
}

// ----------------------------------------------
// 4. Check if path exists
// ----------------------------------------------
auto path_exists(const std::string& path) -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);
  DWORD attr = GetFileAttributesW(wpath.c_str());
  return attr != INVALID_FILE_ATTRIBUTES;
}

// ----------------------------------------------
// 5. Check if path exists and is directory
// ----------------------------------------------
auto path_exists_and_is_directory(const std::string& path) -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);
  DWORD attr = GetFileAttributesW(wpath.c_str());
  return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

auto archive_enabled(const CommandContext<CP_OPTIONS.size()>& ctx) -> bool {
  return ctx.get<bool>("--archive", false) || ctx.get<bool>("-a", false);
}

auto preserve_metadata_enabled(const CommandContext<CP_OPTIONS.size()>& ctx)
    -> bool {
  return archive_enabled(ctx) || ctx.get<bool>("-p", false);
}

auto preserve_metadata(const std::string& srcPath, const std::string& destPath)
    -> cp::Result<bool> {
  std::wstring wsrc = utf8_to_wstring(srcPath);
  std::wstring wdest = utf8_to_wstring(destPath);

  WIN32_FILE_ATTRIBUTE_DATA src_data{};
  if (!GetFileAttributesExW(wsrc.c_str(), GetFileExInfoStandard, &src_data)) {
    return std::unexpected("cannot read source metadata");
  }

  const DWORD flags = (src_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                          ? FILE_FLAG_BACKUP_SEMANTICS
                          : 0;
  HANDLE handle =
      CreateFileW(wdest.c_str(), FILE_WRITE_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return std::unexpected("cannot write destination metadata");
  }

  BOOL time_ok =
      SetFileTime(handle, &src_data.ftCreationTime, &src_data.ftLastAccessTime,
                  &src_data.ftLastWriteTime);
  CloseHandle(handle);
  if (!time_ok) {
    return std::unexpected("cannot preserve timestamps");
  }

  if (!SetFileAttributesW(wdest.c_str(), src_data.dwFileAttributes)) {
    return std::unexpected("cannot preserve attributes");
  }
  return true;
}

auto backup_enabled(const CommandContext<CP_OPTIONS.size()>& ctx) -> bool {
  return ctx.get<bool>("-b", false) || ctx.has("--backup");
}

auto backup_suffix(const CommandContext<CP_OPTIONS.size()>& ctx)
    -> std::string {
  auto suffix = ctx.get<std::string>("--suffix", "");
  if (suffix.empty()) {
    suffix = ctx.get<std::string>("-S", "");
  }
  if (suffix.empty()) {
    suffix = cp_constants::DEFAULT_BACKUP_SUFFIX;
  }
  return suffix;
}

auto backup_existing_destination(const std::string& destPath,
                                 const CommandContext<CP_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  if (!backup_enabled(ctx)) {
    return true;
  }

  std::wstring wdest = utf8_to_wstring(destPath);
  if (GetFileAttributesW(wdest.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return true;
  }

  std::wstring backup_path = wdest + utf8_to_wstring(backup_suffix(ctx));
  if (!MoveFileExW(wdest.c_str(), backup_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    return std::unexpected("cannot create backup for destination");
  }
  return true;
}

auto copy_self_with_backup(const std::string& path,
                           const CommandContext<CP_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  bool force = ctx.get<bool>("--force", false) || ctx.get<bool>("-f", false);
  if (!force || !backup_enabled(ctx)) {
    return std::unexpected("source and destination are the same file");
  }

  std::wstring wpath = utf8_to_wstring(path);
  std::wstring backup_path = wpath + utf8_to_wstring(backup_suffix(ctx));
  if (!CopyFileW(wpath.c_str(), backup_path.c_str(), FALSE)) {
    return std::unexpected("cannot create backup for destination");
  }
  return true;
}

// ----------------------------------------------
// 6. Copy a single file
// ----------------------------------------------
auto copy_file(const std::string& srcPath, const std::string& destPath,
               const CommandContext<CP_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  bool interactive =
      ctx.get<bool>("--interactive", false) || ctx.get<bool>("-i", false);
  bool verbose = ctx.get<bool>("--verbose", false);
  bool no_clobber =
      ctx.get<bool>("--no-clobber", false) || ctx.get<bool>("-n", false);
  bool update = ctx.get<bool>("-u", false) || ctx.get<bool>("--update", false);

  std::error_code equivalent_ec;
  if (std::filesystem::exists(srcPath) && std::filesystem::exists(destPath) &&
      std::filesystem::equivalent(srcPath, destPath, equivalent_ec) &&
      !equivalent_ec) {
    return copy_self_with_backup(srcPath, ctx);
  }

  if (no_clobber && std::filesystem::exists(destPath)) {
    return true;
  }

  if (update && std::filesystem::exists(destPath)) {
    WIN32_FILE_ATTRIBUTE_DATA src_data{};
    WIN32_FILE_ATTRIBUTE_DATA dest_data{};
    std::wstring wsrc = utf8_to_wstring(srcPath);
    std::wstring wdst = utf8_to_wstring(destPath);
    if (GetFileAttributesExW(wsrc.c_str(), GetFileExInfoStandard, &src_data) &&
        GetFileAttributesExW(wdst.c_str(), GetFileExInfoStandard, &dest_data)) {
      if (CompareFileTime(&src_data.ftLastWriteTime,
                          &dest_data.ftLastWriteTime) <= 0) {
        return true;
      }
    }
  }

  if (interactive) {
    std::ifstream destTest(destPath);
    if (destTest.good()) {
      // OPTIMIZED: Avoid wstring concatenation
      safeErrorPrint("cp: overwrite '");
      safeErrorPrint(destPath);
      safeErrorPrint("'? (y/n) ");
      char response;
      std::cin.get(response);
      if (response != 'y' && response != 'Y') {
        return true;
      }
    }
  }

  auto backupResult = backup_existing_destination(destPath, ctx);
  if (!backupResult) {
    return backupResult;
  }

  // Check if source file exists and is readable
  std::ifstream src(srcPath, std::ios::binary);
  if (!src) {
    return std::unexpected("cannot open for reading");
  }

  // Open destination file
  std::ofstream dest(destPath, std::ios::binary);
  if (!dest) {
    bool force = ctx.get<bool>("--force", false) || ctx.get<bool>("-f", false);
    if (!force) {
      return std::unexpected("cannot open for writing");
    }

    std::wstring wdest = utf8_to_wstring(destPath);
    SetFileAttributesW(wdest.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(wdest.c_str());
    dest.open(destPath, std::ios::binary);
    if (!dest) {
      return std::unexpected("cannot open for writing");
    }
  }

  // Copy file content
  dest << src.rdbuf();
  if (!dest) {
    return std::unexpected("error writing");
  }

  // Flush and close the files
  dest.flush();
  dest.close();
  src.close();

  if (preserve_metadata_enabled(ctx)) {
    auto preserveResult = preserve_metadata(srcPath, destPath);
    if (!preserveResult) return preserveResult;
  }

  if (verbose) {
    // OPTIMIZED: Avoid wstring concatenation
    safePrint("'");
    safePrint(srcPath);
    safePrint("' -> '");
    safePrint(destPath);
    safePrint("'\n");
  }

  return true;
}

// ----------------------------------------------
// 7. Copy directory recursively
// ----------------------------------------------
// Helper function with depth limit
auto copy_directory_helper(const std::string& srcPath,
                           const std::string& destPath,
                           const CommandContext<CP_OPTIONS.size()>& ctx,
                           int depth) -> cp::Result<bool> {
  // Prevent deep recursion
  if (depth > 100) {
    return std::unexpected("maximum recursion depth exceeded");
  }

  // Prevent copying directory into itself
  if (srcPath == destPath) {
    return true;
  }

  // Prevent copying directory where destination is a subdirectory of source
  if (destPath.find(srcPath) == 0 && destPath.size() > srcPath.size() &&
      (destPath[srcPath.size()] == '\\' || destPath[srcPath.size()] == '/')) {
    // OPTIMIZED: Avoid wstring concatenation
    safeErrorPrint("cp: cannot copy directory '");
    safeErrorPrint(srcPath);
    safeErrorPrint("' into itself '");
    safeErrorPrint(destPath);
    safeErrorPrint("'\n");
    return std::unexpected("cannot copy directory into itself");
  }

  // Create destination directory if it doesn't exist
  auto createResult = create_directory_recursive(destPath);
  if (!createResult) {
    return createResult;
  }

  // Open source directory
  std::wstring wsrcPath = utf8_to_wstring(srcPath);
  std::wstring searchPath = wsrcPath + L"\\*";
  WIN32_FIND_DATAW findData;
  HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    return std::unexpected("cannot open directory");
  }

  bool success = true;
  bool verbose = ctx.get<bool>("--verbose", false);

  // Process each item in the directory
  do {
    // Skip . and ..
    if (wcscmp(findData.cFileName, L".") == 0 ||
        wcscmp(findData.cFileName, L"..") == 0) {
      continue;
    }

    // Get the full path of the source file/directory
    int fileNameLength = WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1,
                                             NULL, 0, NULL, NULL);
    if (fileNameLength <= 0) {
      continue;
    }
    // Subtract 1 to exclude the null terminator
    int actualLength = fileNameLength - 1;
    std::string fileName(actualLength, 0);
    WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, &fileName[0],
                        fileNameLength, NULL, NULL);
    // Remove the null terminator if present
    fileName.resize(actualLength);

    std::string srcItemPath = srcPath + "\\" + fileName;
    std::string destItemPath = destPath + "\\" + fileName;

    // Check if it's a directory
    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // Skip . and .. directories (already handled earlier)
      if (wcscmp(findData.cFileName, L".") == 0 ||
          wcscmp(findData.cFileName, L"..") == 0) {
        continue;
      }

      // Verify it's actually a directory
      DWORD attr = GetFileAttributesW(utf8_to_wstring(srcItemPath).c_str());
      if (attr != INVALID_FILE_ATTRIBUTES &&
          (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        // Recursively copy subdirectory with increased depth
        auto subDirResult =
            copy_directory_helper(srcItemPath, destItemPath, ctx, depth + 1);
        if (!subDirResult) {
          success = false;
        }
      }
    } else {
      // Copy file
      auto fileResult = copy_file(srcItemPath, destItemPath, ctx);
      if (!fileResult) {
        success = false;
      }
    }
  } while (FindNextFileW(hFind, &findData));

  FindClose(hFind);

  if (success && preserve_metadata_enabled(ctx)) {
    auto preserveResult = preserve_metadata(srcPath, destPath);
    if (!preserveResult) return preserveResult;
  }

  return success;
}

// Public copy_directory function
auto copy_directory(const std::string& srcPath, const std::string& destPath,
                    const CommandContext<CP_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  return copy_directory_helper(srcPath, destPath, ctx, 0);
}

// ----------------------------------------------
// 8. Process each source path
// ----------------------------------------------
auto process_source_paths(
    const std::tuple<std::vector<std::string>, std::string, bool>& pathsAndDir,
    const CommandContext<CP_OPTIONS.size()>& ctx) -> cp::Result<bool> {
  const auto& [sourcePaths, destPath, destIsDir] = pathsAndDir;
  bool recursive = ctx.get<bool>("--recursive", false);
  recursive |= ctx.get<bool>("-r", false);
  recursive |= ctx.get<bool>("-R", false);
  recursive |= archive_enabled(ctx);
  bool success = true;

  for (const auto& srcPath : sourcePaths) {
    // Check if source path exists
    auto existsResult = path_exists(srcPath);
    if (!existsResult || !*existsResult) {
      // OPTIMIZED: Avoid wstring concatenation
      safeErrorPrint("cp: cannot stat '");
      safeErrorPrint(srcPath);
      safeErrorPrint("': No such file or directory\n");
      success = false;
      continue;
    }

    // Check if source is directory
    auto isDirResult = path_exists_and_is_directory(srcPath);
    if (!isDirResult) {
      success = false;
      continue;
    }

    bool srcIsDir = *isDirResult;
    std::string finalDestPath = destPath;

    if (destIsDir) {
      // If destination is a directory, append source filename
      std::wstring wsrcPath = utf8_to_wstring(srcPath);
      LPWSTR fileName = PathFindFileNameW(wsrcPath.c_str());

      // OPTIMIZED: Use stack buffer
      char fileNameBuf[MAX_PATH * 3];
      int fileNameLength =
          WideCharToMultiByte(CP_UTF8, 0, fileName, -1, fileNameBuf,
                              sizeof(fileNameBuf), NULL, NULL);

      if (fileNameLength > 0 && fileNameLength < sizeof(fileNameBuf)) {
        finalDestPath += "\\" + std::string(fileNameBuf);
      } else {
        // Fallback to dynamic allocation if needed
        std::string fileNameStr(fileNameLength, 0);
        WideCharToMultiByte(CP_UTF8, 0, fileName, -1, &fileNameStr[0],
                            fileNameLength, NULL, NULL);
        finalDestPath += "\\" + fileNameStr;
      }
    }

    if (srcIsDir) {
      if (recursive) {
        auto dirResult = copy_directory(srcPath, finalDestPath, ctx);
        if (!dirResult) {
          // OPTIMIZED: Avoid wstring concatenation
          safeErrorPrint("cp: error copying directory '");
          safeErrorPrint(srcPath);
          safeErrorPrint("'\n");
          success = false;
        }
      } else {
        // OPTIMIZED: Avoid wstring concatenation
        safeErrorPrint("cp: omitting directory '");
        safeErrorPrint(srcPath);
        safeErrorPrint("'\n");
        success = false;
      }
    } else {
      auto fileResult = copy_file(srcPath, finalDestPath, ctx);
      if (!fileResult) {
        // OPTIMIZED: Avoid wstring concatenation
        safeErrorPrint("cp: error copying file '");
        safeErrorPrint(srcPath);
        safeErrorPrint("'\n");
        success = false;
      }
    }
  }

  return success;
}

// ----------------------------------------------
// 9. Main pipeline
// ----------------------------------------------
template <size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<bool> {
  bool no_clobber =
      ctx.get<bool>("--no-clobber", false) || ctx.get<bool>("-n", false);
  if (no_clobber && backup_enabled(ctx)) {
    return std::unexpected("options --backup and --no-clobber are mutually exclusive");
  }

  bool no_target_directory = ctx.get<bool>("-T", false) ||
                             ctx.get<bool>("--no-target-directory", false);
  return validate_arguments(ctx)
      .and_then([&](std::pair<std::vector<std::string>, std::string> paths) {
        return check_destination(paths, no_target_directory);
      })
      .and_then([&](std::tuple<std::vector<std::string>, std::string, bool>
                        pathsAndDir) {
        return process_source_paths(pathsAndDir, ctx);
      });
}

}  // namespace cp_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    cp,
    /* name */
    "cp",

    /* synopsis */
    "copy files and directories",

    /* description */
    "Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n"
    "\n"
    "In the first form, copy SOURCE to DEST.\n"
    "In the second form, copy each SOURCE to DIRECTORY.",

    /* examples */
    "  cp file1.txt file2.txt       Copy file1.txt to file2.txt\n"
    "  cp -r dir1 dir2              Recursively copy dir1 to dir2\n"
    "  cp -v file.txt dir/           Verbose copy file.txt to dir/\n"
    "  cp -i file.txt file.txt       Interactive copy (prompt before "
    "overwrite)",

    /* see also */
    "mv(1), rm(1), ln(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    CP_OPTIONS) {
  using namespace cp_pipeline;
  using namespace core::pipeline;

  auto result = process_command(ctx);
  if (!result) {
    report_error(result, L"cp");
    return 1;
  }

  return *result ? 0 : 1;
}
