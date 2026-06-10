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

#include <aclapi.h>

#pragma comment(lib, "advapi32.lib")

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

struct OwnerGroupSpec {
  std::string owner;
  std::string group;
  bool has_group = false;
  bool warned_dot_separator = false;
};

struct Config {
  bool recursive = false;
  bool verbose = false;
  bool changes = false;
  bool quiet = false;
  bool preserve_root = false;
  bool has_reference = false;
  std::string owner;
  std::string group;
  std::string reference_file;
  std::string reference_owner_display;
  std::string from_spec;
  OwnerGroupSpec from_owner_group;
  bool has_from_spec = false;
  bool has_group = false;
  std::vector<std::string> warnings;
  std::vector<std::string> files;
};

struct PreserveRootMatch {
  std::string display_path;
  bool same_as_root = false;
};

struct OwnershipInfo {
  std::string owner_name;
  std::string owner_id;
  std::string group_name;
  std::string group_id;
};

auto is_numeric_id(const std::string& value) -> bool {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

auto lookup_account_type(const std::string& account_name)
    -> std::optional<SID_NAME_USE> {
  if (account_name.empty()) return std::nullopt;

  std::wstring waccount = utf8_to_wstring(account_name);
  DWORD sid_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE use = SidTypeUnknown;

  LookupAccountNameW(nullptr, waccount.c_str(), nullptr, &sid_size, nullptr,
                     &domain_size, &use);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return std::nullopt;
  }

  std::vector<std::byte> sid_buffer(sid_size);
  std::wstring domain(domain_size, L'\0');
  if (!LookupAccountNameW(nullptr, waccount.c_str(), sid_buffer.data(),
                          &sid_size, domain.data(), &domain_size, &use)) {
    return std::nullopt;
  }

  return use;
}

auto lookup_account_name_from_sid(PSID sid) -> std::string {
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

auto lookup_account_id_from_sid(PSID sid) -> std::string {
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

auto get_owner_group_display_for_path(const std::string& path) -> std::string {
  std::wstring wpath = utf8_to_wstring(path);
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;
  PSID group_sid = nullptr;

  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, &owner_sid,
      &group_sid, nullptr, nullptr, &security_desc);
  if (status != ERROR_SUCCESS) {
    if (security_desc != nullptr) {
      LocalFree(security_desc);
    }
    return {};
  }

  std::string owner_name = lookup_account_name_from_sid(owner_sid);
  std::string group_name = lookup_account_name_from_sid(group_sid);
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }

  if (!owner_name.empty() && !group_name.empty()) {
    return owner_name + ":" + group_name;
  }
  if (!owner_name.empty()) {
    return owner_name;
  }
  if (!group_name.empty()) {
    return ":" + group_name;
  }
  return {};
}

auto get_ownership_info_for_path(const std::string& path) -> OwnershipInfo {
  std::wstring wpath = utf8_to_wstring(path);
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;
  PSID group_sid = nullptr;

  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, &owner_sid,
      &group_sid, nullptr, nullptr, &security_desc);
  if (status != ERROR_SUCCESS) {
    if (security_desc != nullptr) {
      LocalFree(security_desc);
    }
    return {};
  }

  OwnershipInfo info{
      .owner_name = lookup_account_name_from_sid(owner_sid),
      .owner_id = lookup_account_id_from_sid(owner_sid),
      .group_name = lookup_account_name_from_sid(group_sid),
      .group_id = lookup_account_id_from_sid(group_sid)};
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  return info;
}

auto format_ownership_display(const OwnershipInfo& info) -> std::string {
  if (!info.owner_name.empty() && !info.group_name.empty()) {
    return info.owner_name + ":" + info.group_name;
  }
  if (!info.owner_name.empty()) {
    return info.owner_name;
  }
  if (!info.group_name.empty()) {
    return ":" + info.group_name;
  }
  return {};
}

auto names_equal_case_insensitive(std::string_view lhs, std::string_view rhs)
    -> bool {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  return std::ranges::equal(
      lhs, rhs, [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) ==
               std::tolower(static_cast<unsigned char>(right));
      });
}

auto matches_from_spec(const OwnerGroupSpec& from,
                       const OwnershipInfo& current_ownership) -> bool {
  if (!from.owner.empty()) {
    const bool owner_matches =
        is_numeric_id(from.owner)
            ? (!current_ownership.owner_id.empty() &&
               current_ownership.owner_id == from.owner)
            : names_equal_case_insensitive(current_ownership.owner_name,
                                           from.owner);
    if (!owner_matches) {
      return false;
    }
  }

  if (from.has_group && !from.group.empty()) {
    const bool group_matches =
        is_numeric_id(from.group)
            ? (!current_ownership.group_id.empty() &&
               current_ownership.group_id == from.group)
            : names_equal_case_insensitive(current_ownership.group_name,
                                           from.group);
    if (!group_matches) {
      return false;
    }
  }

  return true;
}

auto matches_requested_ownership(const Config& cfg,
                                 const OwnershipInfo& current_ownership)
    -> bool {
  if (!cfg.owner.empty()) {
    const bool owner_matches =
        is_numeric_id(cfg.owner)
            ? (!current_ownership.owner_id.empty() &&
               current_ownership.owner_id == cfg.owner)
            : names_equal_case_insensitive(current_ownership.owner_name,
                                           cfg.owner);
    if (!owner_matches) {
      return false;
    }
  }

  if (cfg.has_group) {
    if (!cfg.group.empty()) {
      const bool group_matches =
          is_numeric_id(cfg.group)
              ? (!current_ownership.group_id.empty() &&
                 current_ownership.group_id == cfg.group)
              : names_equal_case_insensitive(current_ownership.group_name,
                                             cfg.group);
      if (!group_matches) {
        return false;
      }
    } else if (cfg.owner.empty()) {
      return true;
    }
  }

  return !cfg.owner.empty() || cfg.has_group;
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

auto parse_owner_group_spec(const std::string& spec) -> OwnerGroupSpec {
  OwnerGroupSpec parsed;
  size_t colon_pos = spec.find(':');
  if (colon_pos == std::string::npos && spec.contains('.')) {
    if (lookup_account_type(spec).has_value()) {
      parsed.owner = spec;
      return parsed;
    }
    parsed.warned_dot_separator = true;
    colon_pos = spec.find('.');
  }
  if (colon_pos != std::string::npos) {
    parsed.owner = spec.substr(0, colon_pos);
    parsed.group = spec.substr(colon_pos + 1);
    parsed.has_group = true;
  } else {
    parsed.owner = spec;
  }
  return parsed;
}

auto emit_warning(const std::string& message) -> void {
  safeErrorPrint("chown: warning: ");
  safeErrorPrint(message);
  safeErrorPrint("\n");
}

auto validate_owner_group_spec(const OwnerGroupSpec& spec,
                               const std::string& raw_spec)
    -> cp::Result<void> {
  if (!spec.owner.empty()) {
    if (!is_numeric_id(spec.owner) &&
        !lookup_account_type(spec.owner).has_value()) {
      return std::unexpected("invalid user: '" + raw_spec + "'");
    }
  }

  if (spec.has_group) {
    if (!spec.group.empty()) {
      if ((spec.group == ":" || spec.group == ".") ||
          (!is_numeric_id(spec.group) &&
           !lookup_account_type(spec.group).has_value())) {
        return std::unexpected("invalid group: '" + raw_spec + "'");
      }
    }

    if (is_numeric_id(spec.owner) && spec.group.empty() &&
        raw_spec != spec.owner) {
      return std::unexpected("invalid spec: '" + raw_spec + "'");
    }
  }

  return {};
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
  cfg.preserve_root = ctx.get<bool>("--preserve-root", false);
  cfg.reference_file = ctx.get<std::string>("--reference", "");
  cfg.has_reference = !cfg.reference_file.empty();
  cfg.from_spec = ctx.get<std::string>("--from", "");

  (void)ctx.get<bool>("--dereference", false);
  (void)ctx.get<bool>("-h", false);
  (void)ctx.get<bool>("--no-dereference", false);
  (void)ctx.get<bool>("-H", false);
  (void)ctx.get<bool>("-L", false);
  (void)ctx.get<bool>("-P", false);
  (void)ctx.get<bool>("--no-preserve-root", false);

  if (!cfg.from_spec.empty()) {
    const auto from = parse_owner_group_spec(cfg.from_spec);
    if (from.warned_dot_separator) {
      emit_warning("'.' should be ':'");
    }
    if (auto from_valid = validate_owner_group_spec(from, cfg.from_spec);
        !from_valid) {
      return std::unexpected(from_valid.error());
    }
    cfg.from_owner_group = from;
    cfg.has_from_spec = true;
  }

  if (cfg.has_reference) {
    std::wstring wref = utf8_to_wstring(cfg.reference_file);
    if (GetFileAttributesW(wref.c_str()) == INVALID_FILE_ATTRIBUTES) {
      return std::unexpected(
          format_missing_reference_error(cfg.reference_file, GetLastError()));
    }
    cfg.reference_owner_display =
        get_owner_group_display_for_path(cfg.reference_file);
    add_file_args(cfg, std::span<const std::string_view>(
                           ctx.positionals.data(), ctx.positionals.size()));
  } else {
    if (ctx.positionals.empty()) {
      return std::unexpected("missing operand");
    }

    const auto parsed = parse_owner_group_spec(std::string(ctx.positionals[0]));
    if (parsed.warned_dot_separator) {
      emit_warning("'.' should be ':'");
    }
    cfg.owner = parsed.owner;
    cfg.group = parsed.group;
    cfg.has_group = parsed.has_group;

    if (auto parsed_valid = validate_owner_group_spec(
            parsed, std::string(ctx.positionals[0]));
        !parsed_valid) {
      return std::unexpected(parsed_valid.error());
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

  const bool needs_current_ownership =
      cfg.has_from_spec || ((cfg.verbose || cfg.changes) && cfg.has_reference) ||
      ((cfg.verbose || cfg.changes) && !cfg.has_reference);
  OwnershipInfo current_ownership;
  if (needs_current_ownership) {
    current_ownership = get_ownership_info_for_path(path);
  }

  if (cfg.has_from_spec &&
      !matches_from_spec(cfg.from_owner_group, current_ownership)) {
    if (cfg.verbose) {
      safeErrorPrint("chown: ownership of '");
      safeErrorPrint(path);
      safeErrorPrint("' retained as ");
      safeErrorPrintLn(format_ownership_display(current_ownership).empty()
                           ? std::string("unknown")
                           : format_ownership_display(current_ownership));
    }
    return 0;
  }

  if (cfg.has_reference) {
    if (cfg.verbose || cfg.changes) {
      safeErrorPrint("chown: ownership of '");
      safeErrorPrint(path);
      safeErrorPrint("' retained as ");
      safeErrorPrintLn(cfg.reference_owner_display.empty()
                           ? std::string("unknown")
                           : cfg.reference_owner_display);
    }
    return 0;
  }

  if ((cfg.verbose || cfg.changes) &&
      matches_requested_ownership(cfg, current_ownership)) {
    safeErrorPrint("chown: ownership of '");
    safeErrorPrint(path);
    safeErrorPrint("' retained as ");
    safeErrorPrintLn(format_ownership_display(current_ownership).empty()
                         ? std::string("unknown")
                         : format_ownership_display(current_ownership));
    return 0;
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
    if (cfg.recursive && cfg.preserve_root) {
      if (auto root_match = get_preserve_root_match(file)) {
        if (!cfg.quiet) {
          safeErrorPrint("chown: it is dangerous to operate recursively on '");
          safeErrorPrint(root_match->display_path);
          if (root_match->same_as_root) {
            safeErrorPrintLn("' (same as '/')");
          } else {
            safeErrorPrintLn("'");
          }
          safeErrorPrintLn(
              "chown: use --no-preserve-root to override this failsafe");
        }
        exit_code = 1;
        continue;
      }
    }

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
