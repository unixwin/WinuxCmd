/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: find.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 WinuxCmd
/// @Description: Implementation for find command.
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
 * @brief FIND command options definition
 *
 * This array defines all the options supported by the find command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -name: Base of file name (the path with the leading directories removed)
 * matches shell pattern PATTERN [IMPLEMENTED]
 * - @a -iname: Like -name, but the match is case insensitive [IMPLEMENTED]
 * - @a -type: File is of type c: b,d,p,f,l,s,D [only d,f,l are supported]
 * [IMPLEMENTED]
 * - @a -mindepth: Descend at least LEVELS levels of directories before tests
 * [IMPLEMENTED]
 * - @a -maxdepth: Descend at most LEVELS levels of directories below
 * starting-points [IMPLEMENTED]
 * - @a -print: Print the full file name on the standard output [IMPLEMENTED]
 * - @a -print0: Print the full file name on the standard output, followed by a
 * null character [IMPLEMENTED]
 * - @a -L: Follow symbolic links [NOT SUPPORT]
 * - @a -H: Do not follow symbolic links, except while processing command line
 * arguments [NOT SUPPORT]
 * - @a -P: Never follow symbolic links (default) [IMPLEMENTED]
 * - @a -delete: Delete files [NOT SUPPORT]
 * - @a -exec: Execute command [NOT SUPPORT]
 * - @a -ok: Execute command after confirmation [NOT SUPPORT]
 * - @a -printf: Print format [NOT SUPPORT]
 * - @a -prune: Prune tree [NOT SUPPORT]
 * - @a -quit: Exit immediately [IMPLEMENTED]
 */
auto constexpr FIND_OPTIONS = std::array{
    OPTION("-name", "",
           "base of file name (the path with the leading directories removed) "
           "matches shell pattern PATTERN",
           STRING_TYPE),
    OPTION("-iname", "", "like -name, but the match is case insensitive",
           STRING_TYPE),
    OPTION("-type", "",
           "file is of type c: b,d,p,f,l,s,D [only d,f,l are supported]",
           STRING_TYPE),
    OPTION("-mindepth", "",
           "descend at least LEVELS levels of directories before tests",
           INT_TYPE),
    OPTION("-maxdepth", "",
           "descend at most LEVELS levels of directories below starting-points",
           INT_TYPE),
    OPTION("-print", "", "print the full file name on the standard output"),
    OPTION("-print0", "",
           "print the full file name on the standard output, followed by a "
           "null character"),
    OPTION("-L", "", "follow symbolic links [NOT SUPPORT]"),
    OPTION("-H", "",
           "do not follow symbolic links, except while processing command line "
           "arguments [NOT SUPPORT]"),
    OPTION("-P", "", "never follow symbolic links (default)"),
    OPTION("-delete", "", "delete files [NOT SUPPORT]"),
    OPTION("-exec", "", "execute command [NOT SUPPORT]", STRING_TYPE),
    OPTION("-ok", "", "execute command after confirmation [NOT SUPPORT]",
           STRING_TYPE),
    OPTION("-printf", "", "print format [NOT SUPPORT]", STRING_TYPE),
    OPTION("-prune", "", "prune tree [NOT SUPPORT]"),
    OPTION("-quit", "", "exit immediately")};

namespace find_pipeline {
namespace cp = core::pipeline;

struct Config {
  SmallVector<std::string, 64> roots;
  std::string name_pattern;
  std::string iname_pattern;
  std::string type_filter;
  int mindepth = 0;
  int maxdepth = std::numeric_limits<int>::max();
  bool has_print = false;
  bool print0 = false;
  bool quit = false;

  bool unsupported_used = false;
  bool had_error = false;
};

// Wildcard matching is now provided by utils:wildcard module

auto type_matches(const std::filesystem::directory_entry& e,
                  std::string_view type) -> bool {
  if (type.empty()) return true;

  if (type == "f") return e.is_regular_file();
  if (type == "d") return e.is_directory();
  if (type == "l") return e.is_symlink();

  return false;
}

auto is_unsupported_used(const CommandContext<FIND_OPTIONS.size()>& ctx)
    -> std::optional<std::string> {
  if (ctx.get<bool>("-L", false)) return "-L is [NOT SUPPORT]";
  if (ctx.get<bool>("-H", false)) return "-H is [NOT SUPPORT]";
  if (ctx.get<bool>("-delete", false)) return "-delete is [NOT SUPPORT]";
  if (!ctx.get<std::string>("-exec", "").empty())
    return "-exec is [NOT SUPPORT]";
  if (!ctx.get<std::string>("-ok", "").empty()) return "-ok is [NOT SUPPORT]";
  if (!ctx.get<std::string>("-printf", "").empty())
    return "-printf is [NOT SUPPORT]";
  if (ctx.get<bool>("-prune", false)) return "-prune is [NOT SUPPORT]";
  return std::nullopt;
}

auto build_config(const CommandContext<FIND_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  if (auto u = is_unsupported_used(ctx); u.has_value()) {
    return std::unexpected(*u);
  }

  cfg.name_pattern = ctx.get<std::string>("-name", "");
  cfg.iname_pattern = ctx.get<std::string>("-iname", "");
  cfg.type_filter = ctx.get<std::string>("-type", "");
  cfg.mindepth = ctx.get<int>("-mindepth", 0);
  cfg.maxdepth = ctx.get<int>("-maxdepth", std::numeric_limits<int>::max());

  cfg.has_print = ctx.get<bool>("-print", false);
  cfg.print0 = ctx.get<bool>("-print0", false);
  cfg.quit = ctx.get<bool>("-quit", false);

  if (!cfg.name_pattern.empty() && !cfg.iname_pattern.empty()) {
    return std::unexpected("cannot use both -name and -iname");
  }

  if (!cfg.type_filter.empty() && cfg.type_filter != "f" &&
      cfg.type_filter != "d" && cfg.type_filter != "l") {
    return std::unexpected("-type currently supports only f,d,l");
  }

  if (cfg.mindepth < 0 || cfg.maxdepth < 0 || cfg.mindepth > cfg.maxdepth) {
    return std::unexpected("invalid depth range");
  }

  for (auto p : ctx.positionals) cfg.roots.emplace_back(p);
  if (cfg.roots.empty()) cfg.roots.emplace_back(".");

  if (!cfg.has_print && !cfg.print0) {
    cfg.has_print = true;  // default action
  }

  return cfg;
}

auto relative_or_self(const std::filesystem::path& root,
                      const std::filesystem::path& p) -> std::filesystem::path {
  std::error_code ec;
  auto rel = std::filesystem::relative(p, root, ec);
  if (ec || rel.empty()) return std::filesystem::path(".");
  return rel;
}

auto depth_from_root(const std::filesystem::path& root,
                     const std::filesystem::path& p) -> int {
  auto rel = relative_or_self(root, p);
  if (rel == ".") return 0;
  int d = 0;
  for (const auto& part : rel) {
    (void)part;
    ++d;
  }
  return d;
}

auto path_display(const std::filesystem::path& p) -> std::string {
  auto s = p.generic_string();
  if (s.empty()) return ".";
  return s;
}

auto entry_matches(const Config& cfg, const std::filesystem::path& p,
                   const std::filesystem::directory_entry& e, int depth)
    -> bool {
  if (depth < cfg.mindepth || depth > cfg.maxdepth) return false;

  auto filename = p.filename().string();
  if (filename.empty()) filename = p.generic_string();

  if (!cfg.name_pattern.empty() &&
      !wildcard_match(cfg.name_pattern, filename, true)) {
    return false;
  }

  if (!cfg.iname_pattern.empty() &&
      !wildcard_match(cfg.iname_pattern, filename, false)) {
    return false;
  }

  if (!type_matches(e, cfg.type_filter)) return false;

  return true;
}

auto print_path(const Config& cfg, std::string_view path) -> void {
  safePrint(path);
  if (cfg.print0) {
    safePrint("\0");
  } else {
    safePrint("\n");
  }
}

auto scan_one_root(const std::filesystem::path& root, Config& cfg,
                   bool& matched_any) -> void {
  std::error_code ec;
  bool exists = std::filesystem::exists(root, ec);
  if (ec || !exists) {
    safeErrorPrint("find: '");
    safeErrorPrint(root.string());
    safeErrorPrint("': No such file or directory\n");
    cfg.had_error = true;
    return;
  }

  std::filesystem::directory_entry root_entry(root, ec);
  if (!ec) {
    int d = 0;
    if (entry_matches(cfg, root, root_entry, d)) {
      print_path(cfg, path_display(root));
      matched_any = true;
      if (cfg.quit) return;
    }
  }

  if (!std::filesystem::is_directory(root, ec) || ec) return;

  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied, ec);
  std::filesystem::recursive_directory_iterator end;

  if (ec) {
    cfg.had_error = true;
    return;
  }

  for (; it != end; ++it) {
    std::error_code iec;
    const auto& de = *it;
    auto p = de.path();
    int d = depth_from_root(root, p);

    if (d > cfg.maxdepth) {
      it.disable_recursion_pending();
      continue;
    }

    if (entry_matches(cfg, p, de, d)) {
      print_path(cfg, path_display(p));
      matched_any = true;
      if (cfg.quit) return;
    }
  }
}

auto process(Config& cfg) -> int {
  bool matched_any = false;
  for (const auto& r : cfg.roots) {
    scan_one_root(std::filesystem::path(r), cfg, matched_any);
    if (cfg.quit && matched_any) break;
  }

  if (cfg.had_error) return 1;
  return 0;
}

}  // namespace find_pipeline

REGISTER_COMMAND(find, "find", "find [path...] [expression]",
                 "Search for files in a directory hierarchy.\n"
                 "If no path is given, '.' is used.",
                 "  find . -name '*.cpp'\n"
                 "  find src -type f -maxdepth 2\n"
                 "  find . -iname 'readme*'",
                 "grep(1), ls(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FIND_OPTIONS) {
  using namespace find_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"find");
    return 1;
  }

  return process(*cfg);
}
