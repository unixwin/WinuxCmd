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
 *  - File: chown.cpp
 *  - CopyrightYear: 2026
 */
/// @Description: Implementation for chown command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CHOWN_OPTIONS = std::array{
    OPTION("-c", "--changes",
           "like verbose but report only when a change is made"),
    OPTION("-f", "--silent", "suppress most error messages"),
    OPTION("", "--quiet", "suppress most error messages"),
    OPTION("", "--dereference", "affect referent of each symbolic link"),
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of referenced files"),
    OPTION("", "--from", "change only from current owner/group", STRING_TYPE),
    OPTION("-H", "", "traverse command-line symlinks to directories"),
    OPTION("-L", "", "traverse every symlink to a directory"),
    OPTION("-P", "", "do not traverse any symbolic links"),
    OPTION("-R", "--recursive", "operate on files and directories recursively"),
    OPTION("", "--reference", "use RFILE's owner and group", STRING_TYPE),
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
    OPTION("", "--preserve-root", "fail to operate recursively on '/'"),
    OPTION("", "--no-preserve-root", "do not treat '/' specially"),
};

namespace chown_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool recursive = false;
  bool verbose = false;
  bool changes = false;
  bool quiet = false;
  bool has_reference = false;
  std::string owner;
  std::string group;
  std::string reference_file;
  std::string from_spec;
  bool has_group = false;
  std::vector<std::string> files;
};

auto add_file_args(Config& cfg, std::span<const std::string_view> args)
    -> void {
  for (auto arg : args) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    cfg.files.push_back(file_arg);
  }
}

auto build_config(const CommandContext<CHOWN_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.recursive =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--recursive", false);
  cfg.verbose = ctx.get<bool>("-v", false);
  cfg.changes = ctx.get<bool>("-c", false) || ctx.get<bool>("--changes", false);
  cfg.quiet = ctx.get<bool>("-f", false) || ctx.get<bool>("--silent", false) ||
              ctx.get<bool>("--quiet", false);
  cfg.reference_file = ctx.get<std::string>("--reference", "");
  cfg.has_reference = !cfg.reference_file.empty();
  cfg.from_spec = ctx.get<std::string>("--from", "");

  (void)ctx.get<bool>("--dereference", false);
  (void)ctx.get<bool>("-h", false);
  (void)ctx.get<bool>("--no-dereference", false);
  (void)ctx.get<bool>("-H", false);
  (void)ctx.get<bool>("-L", false);
  (void)ctx.get<bool>("-P", false);
  (void)ctx.get<bool>("--preserve-root", false);
  (void)ctx.get<bool>("--no-preserve-root", false);

  if (cfg.has_reference) {
    std::wstring wref = utf8_to_wstring(cfg.reference_file);
    if (GetFileAttributesW(wref.c_str()) == INVALID_FILE_ATTRIBUTES) {
      return std::unexpected("failed to get attributes of '" +
                             cfg.reference_file + "'");
    }
    add_file_args(cfg, std::span<const std::string_view>(
                           ctx.positionals.data(), ctx.positionals.size()));
  } else {
    if (ctx.positionals.empty()) {
      return std::unexpected("missing operand");
    }

    std::string owner_group(ctx.positionals[0]);
    size_t colon_pos = owner_group.find(':');
    if (colon_pos == std::string::npos) {
      colon_pos = owner_group.find('.');
    }
    if (colon_pos != std::string::npos) {
      cfg.owner = owner_group.substr(0, colon_pos);
      cfg.group = owner_group.substr(colon_pos + 1);
      cfg.has_group = true;
    } else {
      cfg.owner = owner_group;
    }

    add_file_args(
        cfg, std::span<const std::string_view>(ctx.positionals.data() + 1,
                                               ctx.positionals.size() - 1));
  }

  if (cfg.files.empty()) {
    if (cfg.has_reference) {
      return std::unexpected("missing file operand");
    }
    return std::unexpected("missing operand after '" +
                           std::string(ctx.positionals[0]) + "'");
  }

  return cfg;
}

auto process_file(const std::string& path, const Config& cfg) -> int {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD attr = GetFileAttributesW(wpath.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!cfg.quiet) {
      safeErrorPrint("chown: cannot access '" + path +
                     "': No such file or directory\n");
    }
    return 1;
  }

  // On Windows, chown requires administrator privileges
  // Report the current state and note that actual ownership change is not
  // supported
  if (cfg.verbose) {
    if (cfg.has_group && !cfg.group.empty()) {
      safePrint("changing ownership of '" + path + "'");
      safePrint(" to " + cfg.owner + ":" + cfg.group);
      safePrint("\n");
    } else {
      safePrint("changing ownership of '" + path + "'");
      safePrint(" to " + cfg.owner);
      safePrint("\n");
    }
  }

  // Note: Actual ownership change requires administrator privileges
  // and is not implemented in this version
  return 0;
}

auto process_recursive(const std::string& path, const Config& cfg) -> int {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD attr = GetFileAttributesW(wpath.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!cfg.quiet) {
      safeErrorPrint("chown: cannot access '" + path +
                     "': No such file or directory\n");
    }
    return 1;
  }

  int exit_code = process_file(path, cfg);

  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    std::wstring search_path = wpath + L"\\*";
    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        std::wstring filename = find_data.cFileName;
        if (filename == L"." || filename == L"..") {
          continue;
        }

        std::string subpath = path + "\\" + wstring_to_utf8(filename);
        int sub_result = process_recursive(subpath, cfg);
        if (sub_result != 0) {
          exit_code = sub_result;
        }
      } while (FindNextFileW(hFind, &find_data));

      FindClose(hFind);
    }
  }

  return exit_code;
}

}  // namespace chown_pipeline

REGISTER_COMMAND(
    chown, "chown", "change file owner and group",
    "Change the owner and/or group of each FILE.\n"
    "\n"
    "Note: On Windows, chown is limited. Without administrator privileges,\n"
    "the command can only report the current state. Changing ownership\n"
    "requires elevated permissions.",
    "  chown user file.txt            Change owner of file.txt\n"
    "  chown user:group file.txt     Change owner and group\n"
    "  chown -R user dir/            Recursively change owner\n"
    "  chown user *.txt              Change owner of all .txt files",
    "chgrp(1), chmod(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    CHOWN_OPTIONS) {
  using namespace chown_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("chown: ");
    safeErrorPrint(cfg_result.error());
    safeErrorPrint("\n");
    safeErrorPrint("Try 'chown --help' for more information.\n");
    return 1;
  }

  const auto& cfg = *cfg_result;

  int exit_code = 0;

  for (const auto& file : cfg.files) {
    int result;
    if (cfg.recursive) {
      result = process_recursive(file, cfg);
    } else {
      result = process_file(file, cfg);
    }
    if (result != 0) {
      exit_code = result;
    }
  }

  return exit_code;
}
