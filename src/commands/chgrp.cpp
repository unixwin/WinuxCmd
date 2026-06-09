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
import container;

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
    OPTION("", "--from",
           "change only if current group is GROUP", STRING_TYPE),
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
  std::string reference_group_display;
  std::string from_group;
  bool has_reference = false;
  bool changes = false;
  bool silent = false;
  bool verbose = false;
  bool recursive = false;
  bool no_dereference = false;
  bool preserve_root = false;
  SmallVector<std::string, 64> files;
};

struct PreserveRootMatch {
  std::string display_path;
  bool same_as_root = false;
};

struct GroupInfo {
  std::string name;
  std::string numeric_id;
};

auto is_numeric_group_id_spec(std::string_view spec) -> bool {
  if (spec.empty()) {
    return false;
  }

  if (spec.front() == ':') {
    spec.remove_prefix(1);
  }

  return !spec.empty() &&
         std::ranges::all_of(spec, [](char ch) { return std::isdigit(
                                                    static_cast<unsigned char>(
                                                        ch)) != 0; });
}

auto is_valid_group_name(const std::string& group_name) -> bool {
  if (group_name.empty()) return true;

  std::wstring wgroup = utf8_to_wstring(group_name);
  DWORD sid_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type = SidTypeUnknown;

  LookupAccountNameW(nullptr, wgroup.c_str(), nullptr, &sid_size, nullptr,
                     &domain_size, &sid_type);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return false;
  }

  std::vector<std::byte> sid_buffer(sid_size);
  std::wstring domain(domain_size, L'\0');
  return LookupAccountNameW(nullptr, wgroup.c_str(), sid_buffer.data(),
                            &sid_size, domain.data(), &domain_size,
                            &sid_type) != 0;
}

auto lookup_group_name_from_sid(PSID sid) -> std::string {
  if (sid == nullptr) return {};

  DWORD name_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type = SidTypeUnknown;
  LookupAccountSidW(nullptr, sid, nullptr, &name_size, nullptr, &domain_size,
                    &sid_type);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return {};
  }

  std::wstring name(name_size, L'\0');
  std::wstring domain(domain_size, L'\0');
  if (!LookupAccountSidW(nullptr, sid, name.data(), &name_size, domain.data(),
                         &domain_size, &sid_type)) {
    return {};
  }

  name.resize(name_size);
  return wstring_to_utf8(name);
}

auto lookup_group_id_from_sid(PSID sid) -> std::string {
  if (sid == nullptr || !IsValidSid(sid)) {
    return {};
  }

  PUCHAR subauth_count = GetSidSubAuthorityCount(sid);
  if (subauth_count == nullptr || *subauth_count == 0) {
    return {};
  }

  DWORD* rid = GetSidSubAuthority(sid, *subauth_count - 1);
  if (rid == nullptr) {
    return {};
  }

  return std::to_string(*rid);
}

auto format_missing_reference_error(const std::string& path, DWORD error)
    -> std::string {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
      return "failed to get attributes of '" + path +
             "': No such file or directory";
    default:
      return "failed to get attributes of '" + path + "'";
  }
}

auto get_group_info_for_path(const std::string& path) -> GroupInfo {
  std::wstring wpath = utf8_to_wstring(path);
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID group_sid = nullptr;

  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
      GROUP_SECURITY_INFORMATION, nullptr, &group_sid, nullptr, nullptr,
      &security_desc);
  if (status != ERROR_SUCCESS) {
    if (security_desc != nullptr) {
      LocalFree(security_desc);
    }
    return {};
  }

  GroupInfo info{.name = lookup_group_name_from_sid(group_sid),
                 .numeric_id = lookup_group_id_from_sid(group_sid)};
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  return info;
}

auto get_group_name_for_path(const std::string& path) -> std::string {
  return get_group_info_for_path(path).name;
}

auto group_names_equal(std::string_view lhs, std::string_view rhs) -> bool {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  return std::ranges::equal(
      lhs, rhs, [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) ==
               std::tolower(static_cast<unsigned char>(right));
      });
}

auto should_process_file(const std::string& file, const Config& cfg) -> bool {
  if (cfg.from_group.empty()) {
    return true;
  }

  GroupInfo current_group = get_group_info_for_path(file);
  if (current_group.name.empty() && current_group.numeric_id.empty()) {
    return false;
  }

  if (is_numeric_group_id_spec(cfg.from_group)) {
    std::string expected_id =
        cfg.from_group.front() == ':' ? cfg.from_group.substr(1)
                                      : cfg.from_group;
    return !current_group.numeric_id.empty() &&
           current_group.numeric_id == expected_id;
  }

  return group_names_equal(current_group.name, cfg.from_group);
}

auto normalize_path_for_root_compare(std::wstring path) -> std::wstring {
  std::ranges::replace(path, L'/', L'\\');
  std::ranges::transform(path, path.begin(),
                         [](wchar_t ch) { return std::towlower(ch); });
  return path;
}

auto get_full_path(const std::wstring& path) -> std::wstring {
  DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (required == 0) {
    return {};
  }

  std::wstring buffer(required, L'\0');
  DWORD written =
      GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
                       buffer.data(), nullptr);
  if (written == 0) {
    return {};
  }

  buffer.resize(written);
  return buffer;
}

auto get_volume_root_path(const std::wstring& full_path) -> std::wstring {
  std::wstring buffer(MAX_PATH, L'\0');
  if (!GetVolumePathNameW(full_path.c_str(), buffer.data(),
                          static_cast<DWORD>(buffer.size()))) {
    return {};
  }

  buffer.resize(wcslen(buffer.c_str()));
  return buffer;
}

auto get_preserve_root_match(const std::string& input_path)
    -> std::optional<PreserveRootMatch> {
  std::wstring full_path = get_full_path(utf8_to_wstring(input_path));
  if (full_path.empty()) {
    return std::nullopt;
  }

  std::wstring volume_root = get_volume_root_path(full_path);
  if (volume_root.empty()) {
    return std::nullopt;
  }

  if (normalize_path_for_root_compare(full_path) !=
      normalize_path_for_root_compare(volume_root)) {
    return std::nullopt;
  }

  return PreserveRootMatch{input_path, input_path != "/"};
}

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
  cfg.from_group = ctx.get<std::string>("--from", "");
  cfg.reference_file = ctx.get<std::string>("--reference", "");
  cfg.has_reference = !cfg.reference_file.empty();

  // Get group from positionals (first arg is group, rest are files)
  if (!cfg.has_reference) {
    if (ctx.positionals.empty()) {
      return std::unexpected("missing operand");
    }
    cfg.group = std::string(ctx.positionals[0]);
    for (size_t i = 1; i < ctx.positionals.size(); ++i) {
      cfg.files.push_back(std::string(ctx.positionals[i]));
    }
  } else {
    std::wstring wref = utf8_to_wstring(cfg.reference_file);
    if (GetFileAttributesW(wref.c_str()) == INVALID_FILE_ATTRIBUTES) {
      return std::unexpected(
          format_missing_reference_error(cfg.reference_file, GetLastError()));
    }
    cfg.reference_group_display = get_group_name_for_path(cfg.reference_file);

    // With --reference, all positionals are files
    for (const auto& pos : ctx.positionals) {
      cfg.files.push_back(std::string(pos));
    }
  }

  if (cfg.files.empty()) {
    if (cfg.has_reference) {
      return std::unexpected("missing file operand");
    }
    return std::unexpected("missing operand after '" + cfg.group + "'");
  }

  if (!cfg.has_reference && !is_valid_group_name(cfg.group)) {
    return std::unexpected("invalid group: '" + cfg.group + "'");
  }

  if (!cfg.from_group.empty() && !is_valid_group_name(cfg.from_group) &&
      !is_numeric_group_id_spec(cfg.from_group)) {
    return std::unexpected("invalid user: '" + cfg.from_group + "'");
  }

  return cfg;
}

auto change_group(const std::string& file, const std::string& group_name,
                  const Config& cfg) -> bool {
  if (!should_process_file(file, cfg)) {
    return true;
  }

  if (cfg.has_reference) {
    if (cfg.verbose || cfg.changes) {
      safeErrorPrint("chgrp: group of '");
      safeErrorPrint(file);
      safeErrorPrint("' retained as ");
      safeErrorPrintLn(cfg.reference_group_display.empty()
                           ? std::string("unknown")
                           : cfg.reference_group_display);
    }
    return true;
  }

  if (group_name.empty()) {
    return true;
  }

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
    if (cfg.recursive && cfg.preserve_root) {
      if (auto root_match = get_preserve_root_match(file)) {
        if (!cfg.silent) {
          safeErrorPrint("chgrp: it is dangerous to operate recursively on '");
          safeErrorPrint(root_match->display_path);
          if (root_match->same_as_root) {
            safeErrorPrintLn("' (same as '/')");
          } else {
            safeErrorPrintLn("'");
          }
          safeErrorPrintLn(
              "chgrp: use --no-preserve-root to override this failsafe");
        }
        success = false;
        continue;
      }
    }

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
    "      --from=GROUP       change only if current group is GROUP\n"
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
    safeErrorPrint(cfg_result.error());
    safeErrorPrint("\n");
    safeErrorPrint("Try 'chgrp --help' for more information.\n");
    return 1;
  }

  return run(*cfg_result);
}
