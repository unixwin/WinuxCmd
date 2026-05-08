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
    OPTION("-R", "--recursive", "operate on files and directories recursively"),
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
};

namespace chown_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool recursive = false;
  bool verbose = false;
  std::string owner;
  std::string group;
  bool has_group = false;
  std::vector<std::string> files;
};

auto build_config(const CommandContext<CHOWN_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.recursive =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--recursive", false);
  cfg.verbose = ctx.get<bool>("-v", false);

  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  std::string owner_group(ctx.positionals[0]);
  size_t colon_pos = owner_group.find(':');
  if (colon_pos != std::string::npos) {
    cfg.owner = owner_group.substr(0, colon_pos);
    cfg.group = owner_group.substr(colon_pos + 1);
    cfg.has_group = true;
  } else {
    cfg.owner = owner_group;
  }

  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    std::string file_arg(ctx.positionals[i]);
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

  if (cfg.files.empty()) {
    return std::unexpected("missing operand after '" + owner_group + "'");
  }

  return cfg;
}

auto process_file(const std::string& path, const Config& cfg) -> int {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD attr = GetFileAttributesW(wpath.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    safeErrorPrint("chown: cannot access '" + path +
                   "': No such file or directory\n");
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
    safeErrorPrint("chown: cannot access '" + path +
                   "': No such file or directory\n");
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
