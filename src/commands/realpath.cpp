// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [DIFFERS]
    OPTION("-e", "--canonicalize-existing",
           "all components of the path must exist"),
    // [EXT] not in GNU coreutils realpath
    OPTION("-E", "--canonicalize", "all components but the last must exist"),
    // [DIFFERS]
    OPTION("-m", "--canonicalize-missing", "no path components need to exist"),
    // [DIFFERS]
    OPTION("-L", "--logical", "resolve '..' and '.' before symlinks"),
    // [DIFFERS]
    OPTION("-P", "--physical", "resolve symlinks before '..' and '.'"),
    // [DIFFERS]
    OPTION("-q", "--quiet", "suppress error messages"),
    OPTION("-s", "--strip", "do not expand symlinks"),
    OPTION("", "--no-symlinks", "do not expand symlinks"),
    // [DIFFERS]
    OPTION("", "--relative-to", "print the resolved path relative to DIR",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--relative-base", "print relative paths below DIR",
           STRING_TYPE),
    // [DIFFERS]
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
  bool logical = false;   // -L: resolve ".." before symlinks (accepted, Windows
                          // normalization)
  bool physical = false;  // -P: resolve symlinks before ".." (accepted, Windows
                          // normalization)
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
  return "realpath: " + path + ": No such file or directory";
}

auto empty_operand_error() -> std::string {
  return "invalid operand: empty string";
}

auto not_directory_error(const std::string& path) -> std::string {
  return "realpath: " + path + ": Not a directory";
}

auto canon_error(const std::string& path, int err) -> std::string {
  switch (err) {
    case ELOOP:
      return "realpath: " + path + ": Too many levels of symbolic links";
    case ENOTDIR:
      return not_directory_error(path);
    case ENAMETOOLONG:
      return "realpath: " + path + ": " +
             winux::i18n::format("command.realpath.error.file_name_too_long",
                                 "File name too long");
    default:
      return access_error(path);
  }
}

auto to_canon_mode(CanonicalizationMode mode) -> native_path::CanonMode {
  switch (mode) {
    case CanonicalizationMode::existing:
      return native_path::CanonMode::existing;
    case CanonicalizationMode::missing:
      return native_path::CanonMode::missing;
    case CanonicalizationMode::permissive:
    default:
      return native_path::CanonMode::all_but_last;
  }
}

// [GNU] realpath canonicalizes like canonicalize_filename_mode: each
// component is looked up and symlink targets are substituted as they are
// found (intermediate links like "ldir/f" resolve, a dangling leaf still
// yields its target's path under the default permissive mode).
auto canonicalize_operand(const std::string& path, const Config& cfg)
    -> PathResult {
  auto resolved = native_path::canonicalize_path_w(
      utf8_to_wstring(path), to_canon_mode(cfg.mode), !cfg.no_symlinks,
      cfg.logical && !cfg.physical);
  if (!resolved) {
    return {false, {}, canon_error(path, resolved.error())};
  }
  return {true, wstring_to_utf8(*resolved), {}};
}

auto resolve_relative_option(const std::string& path, const Config& cfg)
    -> PathResult {
  auto resolved = canonicalize_operand(path, cfg);
  if (!resolved) {
    return resolved;
  }

  // GNU treats the -e/-m mode of the operand the same for --relative-to /
  // --relative-base, but the base must still be a usable directory.
  if (cfg.mode == CanonicalizationMode::existing) {
    DWORD attrs = GetFileAttributesW(utf8_to_wstring(resolved.value).c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      return {false, {}, not_directory_error(path)};
    }
  }
  return resolved;
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
  return wstring_to_utf8(relative.generic_wstring());
}

auto emit_error(const std::string& message, bool zero_terminated) -> void {
  safeErrorPrint(message);
  safeErrorPrint(zero_terminated ? char{'\0'} : char{'\n'});
}

auto build_config(const CommandContext<REALPATH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= REALPATH_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.long_name == "--canonicalize-missing" || meta.short_name == "-m") {
      cfg.mode = CanonicalizationMode::missing;
    } else if (meta.long_name == "--canonicalize-existing" ||
               meta.short_name == "-e") {
      cfg.mode = CanonicalizationMode::existing;
    } else if (meta.long_name == "--canonicalize" || meta.short_name == "-E") {
      cfg.mode = CanonicalizationMode::permissive;
    }
  }

  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);
  cfg.no_symlinks = ctx.get<bool>("--strip", false) ||
                    ctx.get<bool>("-s", false) ||
                    ctx.get<bool>("--no-symlinks", false);
  cfg.logical = ctx.get<bool>("--logical", false) || ctx.get<bool>("-L", false);
  cfg.physical =
      ctx.get<bool>("--physical", false) || ctx.get<bool>("-P", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);

  for (const auto& arg : ctx.raw_args) {
    if (arg == "--relative-to=" || arg == "--relative-base=") {
      return std::unexpected(empty_operand_error());
    }
  }

  for (const auto& occurrence :
       ctx.string_occurrences({"--relative-to", "--relative-base"})) {
    if (occurrence.value.empty()) {
      return std::unexpected(empty_operand_error());
    }
    auto normalized = resolve_relative_option(occurrence.value, cfg);
    if (!normalized) {
      return std::unexpected(normalized.error);
    }
    if (occurrence.long_name == "--relative-to") {
      cfg.relative_to = normalized.value;
    } else if (occurrence.long_name == "--relative-base") {
      cfg.relative_base = normalized.value;
    }
  }

  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  for (const auto& arg : ctx.positionals) {
    std::string file_arg(arg);
    if (file_arg.empty()) {
      return std::unexpected(empty_operand_error());
    }
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
    PathResult resolved = canonicalize_operand(path, cfg);

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
    "  -L, --logical                 resolve '..' before symlinks (accepted)\n"
    "  -P, --physical                resolve symlinks before '..' (accepted)\n"
    "  -q, --quiet                   suppress most error messages\n"
    "  -s, --strip, --no-symlinks    do not expand symlinks\n"
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
    if (result.error() == "missing operand") {
      safeErrorPrintLn("realpath: missing operand");
      safeErrorPrintLn("Try 'realpath --help' for more information.");
      return 1;
    }

    cp::report_error(result, L"realpath");
    return 1;
  }

  return process_paths(*result) ? 0 : 1;
}
