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
 * [IMPLEMENTED]
 * - @a -E, @a --canonicalize: all but the last component must exist
 * [IMPLEMENTED]
 * - @a -m, @a --canonicalize-missing: no path components need to exist
 * [IMPLEMENTED]
 * - @a -L, @a --logical: accepted; Windows path normalization is used
 * [PARTIAL]
 * - @a -P, @a --physical: accepted; Windows path normalization is used
 * [PARTIAL]
 * - @a -q, @a --quiet: suppress error messages [IMPLEMENTED]
 * - @a -s, @a --strip: do not expand symlinks [ACCEPTED]
 * - @a -z, @a --zero: end output with NUL byte instead of newline
 * [IMPLEMENTED]
 */
auto constexpr REALPATH_OPTIONS = std::array{
    OPTION("-e", "--canonicalize-existing",
           "all components of the path must exist"),
    OPTION("-E", "--canonicalize", "all components but the last must exist"),
    OPTION("-m", "--canonicalize-missing", "no path components need to exist"),
    OPTION("-L", "--logical", "resolve '..' and '.' before symlinks"),
    OPTION("-P", "--physical", "resolve symlinks before '..' and '.'"),
    OPTION("-q", "--quiet", "suppress error messages"),
    OPTION("-s", "--strip", "do not expand symlinks"),
    OPTION("", "--no-symlinks", "do not expand symlinks"),
    OPTION("", "--relative-to", "print the resolved path relative to DIR",
           STRING_TYPE),
    OPTION("", "--relative-base", "print relative paths below DIR",
           STRING_TYPE),
    OPTION("-z", "--zero", "end output with NUL byte instead of newline")};

// ======================================================
// Pipeline components
// ======================================================
namespace realpath_pipeline {
namespace cp = core::pipeline;

enum class CanonicalizationMode {
  permissive,
  existing,
  missing,
};

struct Config {
  CanonicalizationMode mode = CanonicalizationMode::permissive;
  bool quiet = false;
  bool no_symlinks = false;
  bool zero_terminated = false;
  std::string relative_to;
  std::string relative_base;
  SmallVector<std::string, 32> paths{};
};

struct PathResult {
  bool ok = false;
  std::string value;
  std::string error;

  explicit operator bool() const { return ok; }
};

auto access_error(const std::string& path) -> std::string {
  return "realpath: cannot access '" + path + "': No such file or directory";
}

auto resolve_default_path(const std::string& path) -> PathResult {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD buffer_size = GetFullPathNameW(wpath.c_str(), 0, nullptr, nullptr);
  if (buffer_size == 0) {
    return {false, {}, access_error(path)};
  }

  std::vector<wchar_t> buffer(buffer_size);
  DWORD result =
      GetFullPathNameW(wpath.c_str(), buffer_size, buffer.data(), nullptr);

  if (result == 0 || result >= buffer_size) {
    return {false, {}, access_error(path)};
  }

  std::wstring resolved(buffer.data(), result);

  return {true, wstring_to_utf8(resolved), {}};
}

auto resolve_missing_path(const std::string& path) -> PathResult {
  std::error_code ec;
  std::filesystem::path fs_path = utf8_to_wstring(path);
  std::filesystem::path resolved = std::filesystem::absolute(fs_path, ec);
  if (ec) {
    return {false, {}, access_error(path)};
  }

  resolved = resolved.lexically_normal();

  return {true, wstring_to_utf8(resolved.wstring()), {}};
}

auto validate_path_components(const std::string& path, bool allow_missing_leaf)
    -> PathResult {
  std::error_code ec;
  std::filesystem::path fs_path = utf8_to_wstring(path);
  std::filesystem::path absolute = std::filesystem::absolute(fs_path, ec);
  if (ec) {
    return {false, {}, access_error(path)};
  }

  std::filesystem::path root = absolute.root_path();
  if (root.empty()) {
    return {false, {}, access_error(path)};
  }

  std::filesystem::path current = root;
  auto relative = absolute.relative_path();
  for (auto it = relative.begin(); it != relative.end(); ++it) {
    const auto& part = *it;
    if (part.empty() || part == std::filesystem::path(L".")) {
      continue;
    }

    if (part == std::filesystem::path(L"..")) {
      if (current == root) {
        continue;
      }

      auto parent = current.parent_path();
      if (parent.empty()) {
        parent = root;
      }
      current = parent;
      continue;
    }

    current /= part;
    if (GetFileAttributesW(current.c_str()) == INVALID_FILE_ATTRIBUTES) {
      if (allow_missing_leaf && std::next(it) == relative.end()) {
        auto parent = current.parent_path();
        if (!parent.empty() &&
            GetFileAttributesW(parent.c_str()) != INVALID_FILE_ATTRIBUTES) {
          return {true, {}, {}};
        }
      }
      return {false, {}, access_error(path)};
    }
  }

  return {true, {}, {}};
}

auto is_under_base(const std::filesystem::path& path,
                   const std::filesystem::path& base) -> bool {
  auto path_it = path.begin();
  auto base_it = base.begin();
  while (path_it != path.end() && base_it != base.end()) {
    auto lhs = path_it->wstring();
    auto rhs = base_it->wstring();
    std::ranges::transform(lhs, lhs.begin(),
                           [](wchar_t ch) { return std::towlower(ch); });
    std::ranges::transform(rhs, rhs.begin(),
                           [](wchar_t ch) { return std::towlower(ch); });
    if (lhs != rhs) {
      return false;
    }
    ++path_it;
    ++base_it;
  }
  return base_it == base.end();
}

auto apply_relative_options(const std::string& resolved, const Config& cfg)
    -> std::string {
  if (cfg.relative_to.empty() && cfg.relative_base.empty()) {
    return resolved;
  }

  std::error_code ec;
  std::filesystem::path resolved_path = utf8_to_wstring(resolved);
  std::filesystem::path base = utf8_to_wstring(
      cfg.relative_to.empty() ? cfg.relative_base : cfg.relative_to);
  base = std::filesystem::absolute(base, ec).lexically_normal();
  if (ec) {
    return resolved;
  }

  if (!cfg.relative_base.empty()) {
    std::filesystem::path relative_base =
        std::filesystem::absolute(utf8_to_wstring(cfg.relative_base), ec)
            .lexically_normal();
    if (ec || !is_under_base(resolved_path.lexically_normal(), relative_base)) {
      return resolved;
    }
  }

  auto relative = std::filesystem::relative(resolved_path, base, ec);
  if (ec) {
    return resolved;
  }
  return wstring_to_utf8(relative.wstring());
}

auto emit_error(const std::string& message, bool zero_terminated) -> void {
  safeErrorPrint(message);
  safeErrorPrint(zero_terminated ? char{'\0'} : char{'\n'});
}

auto build_config(const CommandContext<REALPATH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  if (ctx.get<bool>("--canonicalize-missing", false) ||
      ctx.get<bool>("-m", false)) {
    cfg.mode = CanonicalizationMode::missing;
  } else if (ctx.get<bool>("--canonicalize-existing", false) ||
             ctx.get<bool>("-e", false)) {
    cfg.mode = CanonicalizationMode::existing;
  } else if (ctx.get<bool>("--canonicalize", false) ||
             ctx.get<bool>("-E", false)) {
    cfg.mode = CanonicalizationMode::permissive;
  }

  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);
  cfg.no_symlinks = ctx.get<bool>("--strip", false) ||
                    ctx.get<bool>("-s", false) ||
                    ctx.get<bool>("--no-symlinks", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);
  cfg.relative_to = ctx.get<std::string>("--relative-to", "");
  cfg.relative_base = ctx.get<std::string>("--relative-base", "");

  if (ctx.positionals.empty()) {
    cfg.paths.push_back(".");
  } else {
    for (const auto& arg : ctx.positionals) {
      std::string file_arg(arg);
      if (contains_wildcard(file_arg)) {
        auto glob_result = glob_expand(file_arg);
        if (glob_result.expanded) {
          for (const auto& file : glob_result.files) {
            cfg.paths.push_back(wstring_to_utf8(file));
          }
          continue;
        }
      }
      cfg.paths.push_back(file_arg);
    }
  }

  return cfg;
}

/**
 * @brief Process paths and print their absolute paths
 * @param cfg Command configuration
 * @return Result with success status
 */
auto process_paths(const Config& cfg) -> bool {
  bool all_ok = true;

  for (size_t i = 0; i < cfg.paths.size(); ++i) {
    const auto& path = cfg.paths[i];
    PathResult resolved{false, {}, access_error(path)};

    switch (cfg.mode) {
      case CanonicalizationMode::permissive:
        if (auto valid = validate_path_components(path, true); valid) {
          resolved = resolve_default_path(path);
        } else {
          resolved = valid;
        }
        break;
      case CanonicalizationMode::existing: {
        auto existing = validate_path_components(path, false);
        if (existing) {
          resolved = resolve_default_path(path);
        } else {
          resolved = existing;
        }
        break;
      }
      case CanonicalizationMode::missing:
        resolved = resolve_missing_path(path);
        break;
    }

    if (!resolved) {
      if (!cfg.quiet) {
        emit_error(resolved.error, cfg.zero_terminated);
      }
      all_ok = false;
      continue;
    }

    resolved.value = apply_relative_options(resolved.value, cfg);
    safePrint(resolved.value);
    if (cfg.zero_terminated) {
      safePrint(char{'\0'});
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
    "  -E, --canonicalize           all but the last component must exist\n"
    "  -m, --canonicalize-missing   no path components need to exist\n"
    "  -e, --canonicalize-existing   all components must exist\n"
    "  -L, --logical                 resolve '..' and '.' before symlinks\n"
    "  -P, --physical                resolve symlinks before '..' and '.'\n"
    "  -q, --quiet                   suppress most error messages\n"
    "  -s, --strip                   do not expand symlinks\n"
    "      --relative-to=DIR         print the resolved path relative to DIR\n"
    "      --relative-base=DIR       print relative paths below DIR\n"
    "  -z, --zero                    end output with NUL instead of newline",
    "  realpath /tmp/../etc/passwd\n"
    "  realpath -s /tmp/\n"
    "  realpath file.txt",
    "readlink(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    REALPATH_OPTIONS) {
  using namespace realpath_pipeline;

  auto result = build_config(ctx);
  if (!result) {
    cp::report_error(result, L"realpath");
    return 1;
  }

  return process_paths(*result) ? 0 : 1;
}
