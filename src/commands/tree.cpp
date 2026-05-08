/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: tree.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tree command.
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

/**
 * @brief TREE command options definition
 *
 * This array defines all the options supported by the tree command.
 * Each option is described with its short form, long form, and description.
 *
 * @par Options:
 * - @a -a: All files are listed [IMPLEMENTED]
 * - @a -d: List directories only [IMPLEMENTED]
 * - @a -L: Max display depth of the directory tree [IMPLEMENTED]
 * - @a -f: Print the full path prefix for each file [IMPLEMENTED]
 * - @a -I: Do not list files that match the given pattern [IMPLEMENTED]
 * - @a -P: List only those files that match the given pattern [IMPLEMENTED]
 * - @a -C: Colorize the output [IMPLEMENTED]
 * - @a -s: Print the size in bytes of each file [IMPLEMENTED]
 * - @a -t: Sort files by last modification time [IMPLEMENTED]
 * - @a -o: Output to file instead of stdout [IMPLEMENTED]
 */
auto constexpr TREE_OPTIONS = std::array{
    OPTION("-a", "--all", "all files are listed"),
    OPTION("-d", "", "list directories only"),
    OPTION("-L", "", "max display depth of the directory tree", INT_TYPE),
    OPTION("-f", "", "print the full path prefix for each file"),
    OPTION("-I", "", "do not list files that match the given pattern",
           STRING_TYPE),
    OPTION("-P", "", "list only those files that match the given pattern",
           STRING_TYPE),
    OPTION("-C", "", "colorize the output"),
    OPTION("-s", "", "print the size in bytes of each file"),
    OPTION("-t", "", "sort files by last modification time"),
    OPTION("-o", "", "output to file instead of stdout", STRING_TYPE)};

namespace tree_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool show_all = false;
  bool dirs_only = false;
  int max_depth = -1;  // -1 means unlimited
  bool full_path = false;
  std::string exclude_pattern;
  std::string include_pattern;
  bool colorize = false;
  bool show_size = false;
  bool sort_by_time = false;
  std::string output_file;

  bool has_error = false;
};

struct FileInfo {
  std::wstring name;
  std::wstring full_path;
  bool is_dir;
  uint64_t size;
  FILETIME mod_time;
  bool is_hidden;
};

// Character class matching is now provided by utils:wildcard module

// File extension constants
namespace tree_constants {
const std::array<const wchar_t *, 10> COMPRESSED_EXTS = {
    L"zip", L"rar", L"7z",  L"tar", L"gz",
    L"bz2", L"xz",  L"iso", L"cab", L"arc"};
const std::array<const wchar_t *, 10> SCRIPT_EXTS = {
    L"sh", L"bat", L"cmd", L"py", L"pl", L"lua", L"js", L"php", L"rb", L"ps1"};
const std::array<const wchar_t *, 10> SOURCE_EXTS = {
    L"c", L"cpp", L"cc", L"cxx", L"h", L"hpp", L"rs", L"ts", L"java", L"go"};
const std::array<const wchar_t *, 10> MEDIA_EXTS = {
    L"jpg",  L"jpeg", L"png", L"gif", L"bmp",
    L"webp", L"mp4",  L"avi", L"mkv", L"mp3"};
}  // namespace tree_constants

/**
 * @brief Get color for a file based on its extension
 */
auto get_file_color(const std::wstring &filename) -> std::wstring_view {
  if (filename.empty()) return COLOR_FILE;

  // Get extension
  std::wstring ext;
  size_t dot_pos = filename.find_last_of(L".");
  if (dot_pos != std::wstring::npos && dot_pos < filename.length() - 1) {
    ext = filename.substr(dot_pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
  } else {
    return COLOR_FILE;
  }

  // Check for compressed files
  for (const auto *comp_ext : tree_constants::COMPRESSED_EXTS) {
    if (ext == comp_ext) return COLOR_ARCHIVE;
  }

  // Check for script files
  for (const auto *script_ext : tree_constants::SCRIPT_EXTS) {
    if (ext == script_ext) return COLOR_SCRIPT;
  }

  // Check for source code files
  for (const auto *source_ext : tree_constants::SOURCE_EXTS) {
    if (ext == source_ext) return COLOR_SOURCE;
  }

  // Check for media files
  for (const auto *media_ext : tree_constants::MEDIA_EXTS) {
    if (ext == media_ext) return COLOR_MEDIA;
  }

  // Check for executable files
  if (ext == L"exe" || ext == L"com" || ext == L"bat" || ext == L"cmd" ||
      ext == L"ps1") {
    return COLOR_EXEC;
  }

  return COLOR_FILE;
}

// Wildcard matching is now provided by utils:wildcard module

/**
 * @brief Build configuration from command context
 */
auto build_config(const CommandContext<TREE_OPTIONS.size()> &ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.show_all = ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false);
  cfg.dirs_only = ctx.get<bool>("-d", false);
  cfg.max_depth = ctx.get<int>("-L", -1);
  cfg.full_path = ctx.get<bool>("-f", false);
  cfg.exclude_pattern = ctx.get<std::string>("-I", "");
  cfg.include_pattern = ctx.get<std::string>("-P", "");
  cfg.colorize = ctx.get<bool>("-C", false);
  cfg.show_size = ctx.get<bool>("-s", false);
  cfg.sort_by_time = ctx.get<bool>("-t", false);
  cfg.output_file = ctx.get<std::string>("-o", "");

  // Auto-enable color if -C is specified and output is terminal
  if (cfg.colorize && !isOutputConsole() && cfg.output_file.empty()) {
    cfg.colorize = false;
  }

  return cfg;
}

/**
 * @brief Convert FILETIME to time_t for comparison
 */
auto filetime_to_time(const FILETIME &ft) -> time_t {
  ULARGE_INTEGER ull;
  ull.LowPart = ft.dwLowDateTime;
  ull.HighPart = ft.dwHighDateTime;
  return static_cast<time_t>((ull.QuadPart - 116444736000000000ULL) /
                             10000000ULL);
}

/**
 * @brief Collect directory contents recursively
 */
auto collect_directory(const std::wstring &path, const Config &cfg,
                       int current_depth = 0)
    -> cp::Result<std::vector<FileInfo>> {
  if (cfg.max_depth >= 0 && current_depth >= cfg.max_depth) {
    return std::vector<FileInfo>{};
  }

  std::vector<FileInfo> entries;
  std::wstring search_path = path;
  if (search_path.back() != L'\\') {
    search_path += L'\\';
  }
  search_path += L'*';

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    // Return empty for non-existent directories (graceful handling)
    return std::vector<FileInfo>{};
  }

  do {
    std::wstring filename = find_data.cFileName;

    // Skip . and ..
    if (filename == L"." || filename == L"..") {
      continue;
    }

    // Skip hidden files unless -a is specified
    bool is_hidden = (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    if (is_hidden && !cfg.show_all) {
      continue;
    }

    // Check exclude pattern
    if (!cfg.exclude_pattern.empty()) {
      std::wstring wexclude = utf8_to_wstring(cfg.exclude_pattern);
      if (wildcard_match(wexclude, filename)) {
        continue;
      }
    }

    // Check include pattern
    if (!cfg.include_pattern.empty()) {
      std::wstring winclude = utf8_to_wstring(cfg.include_pattern);
      if (!wildcard_match(winclude, filename)) {
        continue;
      }
    }

    bool is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    // Skip files if -d is specified
    if (!is_dir && cfg.dirs_only) {
      continue;
    }

    FileInfo info;
    info.name = filename;
    info.full_path = path;
    if (info.full_path.back() != L'\\') {
      info.full_path += L'\\';
    }
    info.full_path += filename;
    info.is_dir = is_dir;
    info.is_hidden = is_hidden;

    // Calculate file size
    info.size = static_cast<uint64_t>(find_data.nFileSizeLow) |
                (static_cast<uint64_t>(find_data.nFileSizeHigh) << 32);

    info.mod_time = find_data.ftLastWriteTime;

    entries.push_back(info);

  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  // Sort entries
  if (cfg.sort_by_time) {
    std::sort(entries.begin(), entries.end(),
              [](const FileInfo &a, const FileInfo &b) {
                time_t ta = filetime_to_time(a.mod_time);
                time_t tb = filetime_to_time(b.mod_time);
                if (ta != tb) {
                  return ta > tb;  // Newest first
                }
                return a.name < b.name;  // Then by name
              });
  } else {
    // Sort alphabetically
    std::sort(
        entries.begin(), entries.end(),
        [](const FileInfo &a, const FileInfo &b) { return a.name < b.name; });
  }

  return entries;
}

/**
 * @brief Format file size in human-readable format
 */
auto format_size(uint64_t size) -> std::string {
  if (size < 1024) {
    return std::to_string(size) + "B";
  } else if (size < 1024 * 1024) {
    return std::to_string(size / 1024) + "K";
  } else if (size < 1024 * 1024 * 1024) {
    return std::to_string(size / (1024 * 1024)) + "M";
  } else {
    return std::to_string(size / (1024 * 1024 * 1024)) + "G";
  }
}

/**
 * @brief Print tree with proper indentation (console output)
 */
auto print_tree_console(const std::vector<FileInfo> &entries, const Config &cfg,
                        const std::wstring &base_path,
                        const std::wstring &prefix, int current_depth,
                        size_t &total_dirs, size_t &total_files) -> void {
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    bool is_last = (i == entries.size() - 1);

    // Build prefix for this entry
    std::wstring line_prefix = prefix;
    if (is_last) {
      line_prefix += L"\u2514\u2500\u2500 ";
    } else {
      line_prefix += L"\u251C\u2500\u2500 ";
    }

    // Build display path
    std::wstring display_path;
    if (cfg.full_path) {
      display_path = entry.full_path;
    } else {
      display_path = entry.name;
    }

    // Build the complete line
    std::wstring line = line_prefix;

    // Print size if requested
    if (cfg.show_size) {
      line += L"[" + utf8_to_wstring(format_size(entry.size)) + L"] ";
    }

    line += display_path;

    // Apply color and print
    if (cfg.colorize) {
      if (entry.is_dir) {
        safePrint(COLOR_DIR);
      } else if (entry.is_hidden) {
        safePrint(L"\033[37m");  // Gray for hidden files
      } else {
        safePrint(get_file_color(entry.name));
      }
    }

    safePrint(line);
    safePrint(L"\n");

    if (cfg.colorize) {
      safePrint(COLOR_RESET);
    }

    // Count this entry
    if (entry.is_dir) {
      total_dirs++;
    } else {
      total_files++;
    }

    // Recursively process subdirectories
    if (entry.is_dir && (cfg.max_depth < 0 || current_depth < cfg.max_depth)) {
      auto sub_entries =
          collect_directory(entry.full_path, cfg, current_depth + 1);

      if (sub_entries.has_value() && !sub_entries->empty()) {
        std::wstring sub_prefix = prefix;
        if (is_last) {
          sub_prefix += L"    ";
        } else {
          sub_prefix += L"\u2502   ";
        }
        print_tree_console(*sub_entries, cfg, entry.full_path, sub_prefix,
                           current_depth + 1, total_dirs, total_files);
      }
    }
  }
}

/**
 * @brief Print tree with proper indentation (file output)
 */
auto print_tree_file(const std::vector<FileInfo> &entries, const Config &cfg,
                     const std::wstring &base_path, const std::wstring &prefix,
                     int current_depth, std::ofstream &output,
                     size_t &total_dirs, size_t &total_files) -> void {
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    bool is_last = (i == entries.size() - 1);

    // Build prefix for this entry
    std::wstring line_prefix = prefix;
    if (is_last) {
      line_prefix += L"\u2514\u2500\u2500 ";
    } else {
      line_prefix += L"\u251C\u2500\u2500 ";
    }

    // Build display path
    std::wstring display_path;
    if (cfg.full_path) {
      display_path = entry.full_path;
    } else {
      display_path = entry.name;
    }

    // Build the complete line
    std::wstring line = line_prefix;

    // Print size if requested
    if (cfg.show_size) {
      line += L"[" + utf8_to_wstring(format_size(entry.size)) + L"] ";
    }

    line += display_path;

    // Output to file (use UTF-8)
    output << wstring_to_utf8(line) << std::endl;

    // Count this entry
    if (entry.is_dir) {
      total_dirs++;
    } else {
      total_files++;
    }

    // Recursively process subdirectories
    if (entry.is_dir && (cfg.max_depth < 0 || current_depth < cfg.max_depth)) {
      auto sub_entries =
          collect_directory(entry.full_path, cfg, current_depth + 1);

      if (sub_entries.has_value() && !sub_entries->empty()) {
        std::wstring sub_prefix = prefix;
        if (is_last) {
          sub_prefix += L"    ";
        } else {
          sub_prefix += L"\u2502   ";
        }
        print_tree_file(*sub_entries, cfg, entry.full_path, sub_prefix,
                        current_depth + 1, output, total_dirs, total_files);
      }
    }
  }
}

/**
 * @brief Execute tree command
 */
auto execute_tree(const CommandContext<TREE_OPTIONS.size()> &ctx)
    -> cp::Result<bool> {
  // Build configuration
  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    return std::unexpected(cfg_result.error());
  }
  Config cfg = std::move(cfg_result.value());

  // Check if output to file
  bool output_to_file = !cfg.output_file.empty();
  std::ofstream file_output;

  if (output_to_file) {
    std::wstring woutput_file = utf8_to_wstring(cfg.output_file);
    file_output.open(woutput_file);
    if (!file_output.is_open()) {
      return std::unexpected("cannot open output file: " + cfg.output_file);
    }
  }

  // Get target directories (default to current directory)
  std::vector<std::wstring> target_dirs;
  for (auto arg : ctx.positionals) {
    target_dirs.push_back(utf8_to_wstring(std::string(arg)));
  }

  if (target_dirs.empty()) {
    target_dirs.push_back(L".");
  }

  // Process each directory
  for (const auto &dir_path : target_dirs) {
    std::wstring abs_path;
    if (dir_path == L".") {
      wchar_t buffer[MAX_PATH];
      GetCurrentDirectoryW(MAX_PATH, buffer);
      abs_path = buffer;
    } else {
      // Get full path
      wchar_t buffer[MAX_PATH];
      GetFullPathNameW(dir_path.c_str(), MAX_PATH, buffer, nullptr);
      abs_path = buffer;
    }

    // Print header if multiple directories
    if (target_dirs.size() > 1) {
      if (output_to_file) {
        file_output << wstring_to_utf8(abs_path) << std::endl << std::endl;
      } else {
        safePrintLn(abs_path);
        safePrintLn(L"");
      }
    }

    // Check if path exists
    DWORD attrs = GetFileAttributesW(abs_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      if (output_to_file) {
        file_output << "tree: cannot access '" << wstring_to_utf8(abs_path)
                    << "': No such file or directory" << std::endl;
      } else {
        safePrintLn(L"tree: cannot access '" + abs_path +
                    L"': No such file or directory");
      }
      return false;
    }

    // Check if it's a directory
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      // It's a file, just print it
      if (output_to_file) {
        file_output << wstring_to_utf8(abs_path) << std::endl;
      } else {
        safePrintLn(abs_path);
      }
      continue;
    }

    // Collect and print directory tree
    auto entries = collect_directory(abs_path, cfg, 0);
    if (entries.has_value()) {
      size_t total_dirs = 0, total_files = 0;

      if (output_to_file) {
        file_output << wstring_to_utf8(abs_path) << std::endl;
        print_tree_file(*entries, cfg, abs_path, L"", 0, file_output,
                        total_dirs, total_files);
      } else {
        safePrintLn(abs_path);
        print_tree_console(*entries, cfg, abs_path, L"", 0, total_dirs,
                           total_files);

        // Print summary
        safePrintLn(std::to_wstring(total_dirs) + L" directories, " +
                    std::to_wstring(total_files) + L" files");
      }
    }
  }

  return true;
}

/**
 * @brief Main pipeline
 * @param ctx Command context
 * @return Result with success status
 */
template <size_t N>
auto process_command(const CommandContext<N> &ctx) -> cp::Result<bool> {
  return execute_tree(ctx);
}

}  // namespace tree_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    tree,
    /* cmd_name */ "tree",
    /* cmd_synopsis */ "list contents of directories in a tree-like format",
    /* cmd_desc */
    "tree is a recursive directory listing program that produces a depth "
    "indented\n"
    "listing of files. Color is supported ala dircolors if the LS_COLORS "
    "environment\n"
    "variable is set, outputting to tty.\n\n"
    "With no arguments, tree lists the files in the current directory. When "
    "directory\n"
    "arguments are given, tree lists all the files and/or directories found in "
    "the\n"
    "given directories each in turn. Upon completion of listing all "
    "files/directories\n"
    "found, tree returns the total number of files and/or directories "
    "listed.\n\n"
    "By default, when a symbolic link is encountered, the path that the "
    "symbolic link\n"
    "refers to is printed after the name of the link in the format:\n"
    "    name -> real-path\n\n"
    "If the -f option is given, then each entry is printed with its full path "
    "prefix.",
    /* examples */
    "  tree              # List files in current directory\n"
    "  tree -L 2         # List with depth limit of 2\n"
    "  tree -d           # List directories only\n"
    "  tree -a           # List all files including hidden\n"
    "  tree -f           # Print full paths\n"
    "  tree -I '*.tmp'   # Exclude tmp files\n"
    "  tree -P '*.cpp'   # Only show cpp files\n"
    "  tree -C           # Colorize output\n"
    "  tree -s           # Show file sizes\n"
    "  tree -t           # Sort by modification time\n"
    "  tree -o out.txt   # Output to file\n"
    "  tree /path/to/dir # List specific directory",
    /* see_also */ "ls, find, du",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    TREE_OPTIONS) {
  using namespace tree_pipeline;
  using namespace core::pipeline;

  auto result = process_command(ctx);
  if (!result) {
    report_error(result, L"tree");
    return 1;
  }

  return *result ? 0 : 1;
}
