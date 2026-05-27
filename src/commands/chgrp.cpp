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
 *  - File: chgrp.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for chgrp.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

#include <aclapi.h>
#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CHGRP_OPTIONS = std::array{
    OPTION("-c", "--changes", "like verbose but report only when a change is made"),
    OPTION("-f", "--silent", "suppress most error messages"),
    OPTION("-f", "--quiet", "suppress most error messages"),
    OPTION("-v", "--verbose",
           "output a diagnostic for every file processed"),
    OPTION("-R", "--recursive", "operate on files and directories recursively"),
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of any referenced file"),
    OPTION("", "--dereference",
           "affect the referent of each symbolic link (default)"),
    OPTION("-H", "",
           "command line symbolic links are followed"),
    OPTION("-L", "",
           "indirect symbolic links are followed"),
    OPTION("-P", "",
           "no symbolic links are followed (default)"),
    OPTION("", "--reference",
           "use RFILE's group instead of specifying GROUP", STRING_TYPE),
    OPTION("", "--preserve-root",
           "fail to operate recursively on '/'"),
    OPTION("", "--no-preserve-root",
           "do not treat '/' specially (default)")};

namespace chgrp_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::string group;
  std::string reference_file;
  bool changes = false;
  bool silent = false;
  bool verbose = false;
  bool recursive = false;
  bool no_dereference = false;
  bool preserve_root = false;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<CHGRP_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.changes = ctx.get<bool>("-c", false) || ctx.get<bool>("--changes", false);
  cfg.silent = ctx.get<bool>("-f", false) || ctx.get<bool>("--silent", false) ||
               ctx.get<bool>("--quiet", false);
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  cfg.recursive = ctx.get<bool>("-R", false) || ctx.get<bool>("--recursive", false);
  cfg.no_dereference =
      ctx.get<bool>("-h", false) || ctx.get<bool>("--no-dereference", false);
  cfg.preserve_root = ctx.get<bool>("--preserve-root", false);
  cfg.reference_file = ctx.get<std::string>("--reference", "");

  // Get group from positionals (first arg is group, rest are files)
  if (cfg.reference_file.empty()) {
    if (ctx.positionals.empty()) {
      return std::unexpected("missing operand");
    }
    cfg.group = std::string(ctx.positionals[0]);
    for (size_t i = 1; i < ctx.positionals.size(); ++i) {
      cfg.files.push_back(std::string(ctx.positionals[i]));
    }
  } else {
    // With --reference, all positionals are files
    for (const auto& pos : ctx.positionals) {
      cfg.files.push_back(std::string(pos));
    }
  }

  if (cfg.files.empty()) {
    return std::unexpected("missing file operand");
  }

  return cfg;
}

auto change_group(const std::string& file, const std::string& group_name,
                  const Config& cfg) -> bool {
  // On Windows, group management is complex and requires admin privileges
  // We use LookupAccountName + SetNamedSecurityInfo
  // For now, we warn that this is a Windows-specific limitation

  // Convert to wide strings
  std::wstring wfile = utf8_to_wstring(file);
  std::wstring wgroup = utf8_to_wstring(group_name);

  // Lookup the group SID
  DWORD sid_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type;

  // First call to get buffer sizes
  LookupAccountNameW(NULL, wgroup.c_str(), NULL, &sid_size, NULL, &domain_size,
                     &sid_type);

  if (sid_size == 0) {
    if (!cfg.silent) {
      safeErrorPrint("chgrp: invalid group '");
      safeErrorPrint(group_name);
      safeErrorPrintLn("'");
    }
    return false;
  }

  std::vector<BYTE> sid(sid_size);
  std::vector<wchar_t> domain(domain_size);

  if (!LookupAccountNameW(NULL, wgroup.c_str(), sid.data(), &sid_size,
                          domain.data(), &domain_size, &sid_type)) {
    if (!cfg.silent) {
      safeErrorPrint("chgrp: cannot look up group '");
      safeErrorPrint(group_name);
      safeErrorPrintLn("'");
    }
    return false;
  }

  // Set the group on the file
  DWORD result = SetNamedSecurityInfoW(
      &wfile[0], SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
      NULL,                        // pOwner
      sid.data(),                  // pGroup
      NULL,                        // pDacl
      NULL                         // pSacl
  );

  if (result != ERROR_SUCCESS) {
    if (!cfg.silent) {
      safeErrorPrint("chgrp: changing group of '");
      safeErrorPrint(file);
      safeErrorPrint("': ");
      if (result == ERROR_ACCESS_DENIED) {
        safeErrorPrintLn("Operation not permitted");
      } else {
        safeErrorPrintLn("Unknown error");
      }
    }
    return false;
  }

  if (cfg.verbose || cfg.changes) {
    safePrint("group of '");
    safePrint(file);
    safePrint("' changed to ");
    safePrintLn(group_name);
  }

  return true;
}

auto process_file(const std::string& file, const Config& cfg) -> bool {
  if (cfg.preserve_root && file == "/") {
    if (!cfg.silent) {
      safeErrorPrintLn("chgrp: it is dangerous to operate recursively on '/'");
    }
    return false;
  }

  return change_group(file, cfg.group, cfg);
}

auto process_directory(const std::wstring& path, const Config& cfg) -> bool {
  WIN32_FIND_DATAW find_data;
  std::wstring search_path = path + L"\\*";
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) return true;

  bool success = true;
  do {
    std::wstring name = find_data.cFileName;
    if (name == L"." || name == L"..") continue;

    std::wstring full_path = path + L"\\" + name;
    std::string full_path_utf8 = wstring_to_utf8(full_path);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      change_group(full_path_utf8, cfg.group, cfg);
      if (cfg.recursive) {
        process_directory(full_path, cfg);
      }
    } else {
      if (!change_group(full_path_utf8, cfg.group, cfg)) {
        success = false;
      }
    }
  } while (FindNextFileW(hFind, &find_data));

  FindClose(hFind);
  return success;
}

auto run(const Config& cfg) -> int {
  bool success = true;

  for (const auto& file : cfg.files) {
    std::wstring wfile = utf8_to_wstring(file);
    DWORD attrs = GetFileAttributesW(wfile.c_str());

    if (attrs == INVALID_FILE_ATTRIBUTES) {
      if (!cfg.silent) {
        safeErrorPrint("chgrp: cannot access '");
        safeErrorPrint(file);
        safeErrorPrintLn("': No such file or directory");
      }
      success = false;
      continue;
    }

    // Change group on the file/directory itself
    if (!change_group(file, cfg.group, cfg)) {
      success = false;
      continue;
    }

    // If recursive and directory, process contents
    if (cfg.recursive && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      if (!process_directory(wfile, cfg)) {
        success = false;
      }
    }
  }

  return success ? 0 : 1;
}

}  // namespace chgrp_pipeline

REGISTER_COMMAND(
    chgrp, "chgrp", "chgrp [OPTION]... GROUP FILE...",
    "Change the group of each FILE to GROUP.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -c, --changes          like verbose but report only when a change is made\n"
    "  -f, --silent           suppress most error messages\n"
    "  -f, --quiet            suppress most error messages\n"
    "  -v, --verbose          output a diagnostic for every file processed\n"
    "  -R, --recursive        operate on files and directories recursively\n"
    "  -h, --no-dereference   affect symbolic links instead of any referenced file\n"
    "      --dereference      affect the referent of each symbolic link (default)\n"
    "  -H                     command line symbolic links are followed\n"
    "  -L                     indirect symbolic links are followed\n"
    "  -P                     no symbolic links are followed (default)\n"
    "      --reference=RFILE  use RFILE's group instead of specifying GROUP\n"
    "      --preserve-root    fail to operate recursively on '/'\n"
    "      --no-preserve-root do not treat '/' specially (default)\n"
    "\n"
    "Note: On Windows, this requires administrator privileges.\n"
    "The GROUP must be a valid Windows group name.",
    "  chgrp staff file.txt       change group to 'staff'\n"
    "  chgrp -R staff /tmp/dir    change group recursively\n"
    "  chgrp --reference=file f2  use file's group for f2",
    "chown(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CHGRP_OPTIONS) {
  using namespace chgrp_pipeline;
  using namespace core::pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("chgrp: ");
    safeErrorPrintLn(cfg_result.error());
    return 1;
  }

  return run(*cfg_result);
}
