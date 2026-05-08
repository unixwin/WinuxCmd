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
 *  - File: realpath.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for realpath - print the absolute path of files
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
 * @brief REALPATH command options definition
 *
 * This array defines all the options supported by the realpath command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -e, @a --canonicalize-existing: all components of the path must exist
 * [TODO]
 * - @a -m, @a --canonicalize-missing: no path components need to exist [TODO]
 * - @a -L, @a --logical: resolve '..' and '.' before symlinks [TODO]
 * - @a -P, @a --physical: resolve symlinks before '..' and '.' [TODO]
 * - @a -q, @a --quiet: suppress error messages [TODO]
 * - @a -s, @a --strip: remove trailing separators [TODO]
 * - @a -z, @a --zero: end output with NUL byte instead of newline [TODO]
 */
auto constexpr REALPATH_OPTIONS = std::array{
    OPTION("-e", "--canonicalize-existing",
           "all components of the path must exist"),
    OPTION("-m", "--canonicalize-missing", "no path components need to exist"),
    OPTION("-L", "--logical", "resolve '..' and '.' before symlinks"),
    OPTION("-P", "--physical", "resolve symlinks before '..' and '.'"),
    OPTION("-q", "--quiet", "suppress error messages"),
    OPTION("-s", "--strip", "remove trailing separators"),
    OPTION("-z", "--zero", "end output with NUL byte instead of newline")};

// ======================================================
// Pipeline components
// ======================================================
namespace realpath_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Get absolute path for a given path
 * @param path Path to resolve
 * @param strip Trailing separator should be removed
 * @return Absolute path or error message
 */
auto get_absolute_path(const std::string& path, bool strip) -> std::string {
  std::wstring wpath = utf8_to_wstring(path);

  // Allocate buffer for the result
  DWORD buffer_size = GetFullPathNameW(wpath.c_str(), 0, nullptr, nullptr);
  if (buffer_size == 0) {
    return "realpath: cannot access '" + path +
           "': " + std::to_string(GetLastError());
  }

  std::vector<wchar_t> buffer(buffer_size);
  DWORD result =
      GetFullPathNameW(wpath.c_str(), buffer_size, buffer.data(), nullptr);

  if (result == 0 || result >= buffer_size) {
    return "realpath: cannot access '" + path +
           "': " + std::to_string(GetLastError());
  }

  std::wstring resolved(buffer.data(), result);  // Construct from known size

  // Strip trailing separators if requested
  if (strip) {
    while (!resolved.empty() &&
           (resolved.back() == L'\\' || resolved.back() == L'/')) {
      resolved.pop_back();
    }
  }

  return wstring_to_utf8(resolved);
}

/**
 * @brief Process paths and print their absolute paths
 * @param ctx Command context
 * @return Result with success status
 */
auto process_paths(const CommandContext<REALPATH_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  bool quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);
  bool strip = ctx.get<bool>("--strip", false) || ctx.get<bool>("-s", false);
  bool zero_terminated =
      ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);

  // Use SmallVector for file paths (max 32 paths) - all stack-allocated
  SmallVector<std::string, 32> paths{};

  if (ctx.positionals.empty()) {
    // If no path is given, use current directory
    paths.push_back(".");
  } else {
    for (const auto& arg : ctx.positionals) {
      std::string file_arg(arg);
      if (contains_wildcard(file_arg)) {
        auto glob_result = glob_expand(file_arg);
        if (glob_result.expanded) {
          for (const auto& file : glob_result.files) {
            paths.push_back(wstring_to_utf8(file));
          }
          continue;
        }
      }
      paths.push_back(file_arg);
    }
  }

  bool all_ok = true;

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    std::string absolute = get_absolute_path(path, strip);

    // Check if result is an error message
    if (absolute.find("realpath:") == 0) {
      if (!quiet) {
        safeErrorPrintLn(absolute);
      }
      all_ok = false;
      continue;
    }

    // Output the absolute path
    safePrint(absolute);
    if (zero_terminated) {
      safePrint("\0");
    } else {
      safePrintLn(L"");
    }
  }

  return all_ok;
}

}  // namespace realpath_pipeline

REGISTER_COMMAND(
    realpath,
    /* name */
    "realpath",

    /* synopsis */
    "print the resolved absolute path",
    "Print the resolved absolute path for each FILE. If no FILE is given,\n"
    "print the resolved absolute path of the current directory.\n\n"
    "All components of the path must exist (no symlinks are followed).\n"
    "The last component may be non-existent.\n\n"
    "Options:\n"
    "  -m, --canonicalize-missing   all components of the path need not exist\n"
    "  -e, --canonicalize-existing   all components must exist\n"
    "  -L, --logical                 resolve '..' and '.' before symlinks\n"
    "  -P, --physical                resolve symlinks before '..' and '.'\n"
    "  -q, --quiet                   suppress most error messages\n"
    "  -s, --strip                   remove trailing separators\n"
    "  -z, --zero                    end output with NUL instead of newline",
    "  realpath /tmp/../etc/passwd\n"
    "  realpath -s /tmp/\n"
    "  realpath file.txt",
    "readlink(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    REALPATH_OPTIONS) {
  using namespace realpath_pipeline;

  auto result = process_paths(ctx);
  if (!result) {
    cp::report_error(result, L"realpath");
    return 1;
  }

  return *result ? 0 : 1;
}
