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

#include <aclapi.h>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CHOWN_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-c", "--changes",
           "like verbose but report only when a change is made"),
    // [DIFFERS]
    OPTION("-f", "--silent", "suppress most error messages"),
    // [DIFFERS]
    OPTION("", "--quiet", "suppress most error messages"),
    // [DIFFERS]
    OPTION("", "--dereference", "affect referent of each symbolic link"),
    // [DIFFERS]
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of referenced files"),
    // [DIFFERS]
    OPTION("", "--from", "change only from current owner/group", STRING_TYPE),
    // [DIFFERS]
    OPTION("-H", "", "traverse command-line symlinks to directories"),
    // [DIFFERS]
    OPTION("-L", "", "traverse every symlink to a directory"),
    // [DIFFERS]
    OPTION("-P", "", "do not traverse any symbolic links"),
    // [DIFFERS]
    OPTION("-R", "--recursive", "operate on files and directories recursively"),
    // [DIFFERS]
    OPTION("", "--reference", "use RFILE's owner and group", STRING_TYPE),
    // [DIFFERS]
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
    // [DIFFERS]
    OPTION("", "--preserve-root", "fail to operate recursively on '/'"),
    // [DIFFERS]
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
  bool no_dereference = false;
  bool traverse_all = false;      // -L
  bool traverse_cmdline = false;  // -H
  std::string owner;
  std::string group;
  std::string reference_file;
  std::string reference_owner_display;
  std::vector<std::byte> reference_owner_sid;
  std::vector<std::byte> reference_group_sid;
  std::string from_spec;
  OwnerGroupSpec from_owner_group;
  bool has_from_spec = false;
  bool has_group = false;
  std::vector<std::string> warnings;
  std::vector<std::string> files;
};

struct PathSids {
  std::vector<std::byte> owner;
  std::vector<std::byte> group;
  bool ok = false;
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
         std::all_of(value.begin(), value.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; });
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

auto copy_sid_bytes(PSID sid) -> std::vector<std::byte> {
  if (sid == nullptr || !IsValidSid(sid)) return {};
  const DWORD len = GetLengthSid(sid);
  std::vector<std::byte> buffer(len);
  if (!CopySid(len, buffer.data(), sid)) {
    return {};
  }
  return buffer;
}

// Fetch owner/group SIDs of the link itself via an OPEN_REPARSE_POINT
// handle; works on dangling symlinks where the named variant follows the
// link and fails.
auto get_sids_for_link(const std::wstring& wpath) -> PathSids {
  PathSids result;
  HANDLE handle = CreateFileW(
      wpath.c_str(), READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return result;
  }
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;
  PSID group_sid = nullptr;
  const DWORD status =
      GetSecurityInfo(handle, SE_FILE_OBJECT,
                      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
                      &owner_sid, &group_sid, nullptr, nullptr, &security_desc);
  if (status == ERROR_SUCCESS) {
    result.owner = copy_sid_bytes(owner_sid);
    result.group = copy_sid_bytes(group_sid);
    result.ok = !result.owner.empty();
  }
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  CloseHandle(handle);
  return result;
}

// Fetch the file object owner/group SIDs without following the final
// component's symbolic link.
auto get_sids_for_path(const std::wstring& wpath) -> PathSids {
  PathSids result;
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;
  PSID group_sid = nullptr;

  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(wpath.c_str()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, &owner_sid,
      &group_sid, nullptr, nullptr, &security_desc);
  if (status == ERROR_SUCCESS) {
    result.owner = copy_sid_bytes(owner_sid);
    result.group = copy_sid_bytes(group_sid);
    result.ok = !result.owner.empty();
  }
  if (security_desc != nullptr) {
    LocalFree(security_desc);
  }
  return result;
}

auto enable_privilege(const wchar_t* name) -> bool {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
    return false;
  }

  LUID luid{};
  bool ok = LookupPrivilegeValueW(nullptr, name, &luid) != FALSE;
  if (ok) {
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    ok = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges),
                               nullptr, nullptr) != FALSE &&
         GetLastError() == ERROR_SUCCESS;
  }
  CloseHandle(token);
  return ok;
}

auto current_token_sid(TOKEN_INFORMATION_CLASS kind) -> std::vector<std::byte> {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return {};
  }

  DWORD required = 0;
  GetTokenInformation(token, kind, nullptr, 0, &required);
  std::vector<std::byte> info(required);
  std::vector<std::byte> sid;
  if (required != 0 &&
      GetTokenInformation(token, kind, info.data(), required, &required)) {
    PSID raw = nullptr;
    if (kind == TokenUser) {
      raw = reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid;
    } else if (kind == TokenPrimaryGroup) {
      raw = reinterpret_cast<TOKEN_PRIMARY_GROUP*>(info.data())->PrimaryGroup;
    }
    sid = copy_sid_bytes(raw);
  }
  CloseHandle(token);
  return sid;
}

// Build a SID sharing every sub-authority of `base` except the final RID,
// which is replaced.  Numeric GNU ids map onto the RID of the file's domain.
auto sid_with_rid(PSID base, DWORD rid) -> std::vector<std::byte> {
  if (base == nullptr || !IsValidSid(base)) return {};

  PUCHAR count_ptr = GetSidSubAuthorityCount(base);
  if (count_ptr == nullptr || *count_ptr == 0) return {};
  const UCHAR count = *count_ptr;

  SID_IDENTIFIER_AUTHORITY authority = *GetSidIdentifierAuthority(base);
  std::vector<DWORD> sub_authorities(count);
  for (UCHAR i = 0; i < count; ++i) {
    DWORD* value = GetSidSubAuthority(base, i);
    if (value == nullptr) return {};
    sub_authorities[i] = (i == count - 1) ? rid : *value;
  }

  PSID built = nullptr;
  if (!AllocateAndInitializeSid(&authority, count, sub_authorities[0],
                                count > 1 ? sub_authorities[1] : 0,
                                count > 2 ? sub_authorities[2] : 0,
                                count > 3 ? sub_authorities[3] : 0,
                                count > 4 ? sub_authorities[4] : 0,
                                count > 5 ? sub_authorities[5] : 0,
                                count > 6 ? sub_authorities[6] : 0,
                                count > 7 ? sub_authorities[7] : 0, &built)) {
    return {};
  }
  auto result = copy_sid_bytes(built);
  FreeSid(built);
  return result;
}

// Resolve an owner/group spec to a SID.  Accepts account names, "S-..." SID
// strings, and bare numeric ids (interpreted as a RID relative to `base_sid`,
// falling back to the current user's SID when the base is unavailable).
auto resolve_account_sid(const std::string& spec, PSID base_sid)
    -> std::vector<std::byte> {
  if (spec.empty()) return {};

  if (spec.size() > 2 && (spec[0] == 'S' || spec[0] == 's') && spec[1] == '-') {
    PSID parsed = nullptr;
    std::wstring wspec = utf8_to_wstring(spec);
    if (ConvertStringSidToSidW(wspec.c_str(), &parsed) && parsed != nullptr) {
      auto result = copy_sid_bytes(parsed);
      LocalFree(parsed);
      return result;
    }
  }

  if (is_numeric_id(spec) &&
      !win32_lookup_account(utf8_to_wstring(spec)).has_value()) {
    DWORD rid = 0;
    try {
      rid = static_cast<DWORD>(std::stoul(spec));
    } catch (...) {
      return {};
    }
    auto sid = sid_with_rid(base_sid, rid);
    if (sid.empty()) {
      auto token_sid = current_token_sid(TokenUser);
      sid = sid_with_rid(token_sid.empty() ? nullptr : token_sid.data(), rid);
    }
    return sid;
  }

  std::wstring waccount = utf8_to_wstring(spec);
  DWORD sid_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE use = SidTypeUnknown;
  LookupAccountNameW(nullptr, waccount.c_str(), nullptr, &sid_size, nullptr,
                     &domain_size, &use);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sid_size == 0) {
    return {};
  }

  std::vector<std::byte> sid(sid_size);
  std::wstring domain(domain_size, L'\0');
  if (!LookupAccountNameW(nullptr, waccount.c_str(), sid.data(), &sid_size,
                          domain.data(), &domain_size, &use)) {
    return {};
  }
  return sid;
}

auto format_chown_error(const std::string& verb, const std::string& path,
                        DWORD error) -> std::string {
  std::string message = "chown: " + verb + " '" + path + "': ";
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      message += "No such file or directory";
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_INVALID_OWNER:
    case ERROR_INVALID_PRIMARY_GROUP:
      message += "Operation not permitted";
      break;
    default:
      message += win32_system_error_text(error);
      break;
  }
  return message;
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

  OwnershipInfo info{.owner_name = lookup_account_name_from_sid(owner_sid),
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

auto matches_from_spec(const OwnerGroupSpec& from,
                       const OwnershipInfo& current_ownership) -> bool {
  if (!from.owner.empty()) {
    const bool owner_matches = win32_account_matches(
        from.owner, current_ownership.owner_name, current_ownership.owner_id);
    if (!owner_matches) {
      return false;
    }
  }

  if (from.has_group && !from.group.empty()) {
    const bool group_matches = win32_account_matches(
        from.group, current_ownership.group_name, current_ownership.group_id);
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
    const bool owner_matches = win32_account_matches(
        cfg.owner, current_ownership.owner_name, current_ownership.owner_id);
    if (!owner_matches) {
      return false;
    }
  }

  if (cfg.has_group) {
    if (!cfg.group.empty()) {
      const bool group_matches = win32_account_matches(
          cfg.group, current_ownership.group_name, current_ownership.group_id);
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
  DWORD written = GetFullPathNameW(
      path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
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

    if (is_numeric_id(spec.owner) &&
        !win32_lookup_account(utf8_to_wstring(spec.owner)).has_value() &&
        spec.group.empty() && raw_spec != spec.owner) {
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
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  cfg.changes = ctx.get<bool>("-c", false) || ctx.get<bool>("--changes", false);
  cfg.quiet = ctx.get<bool>("-f", false) || ctx.get<bool>("--silent", false) ||
              ctx.get<bool>("--quiet", false);
  cfg.preserve_root = ctx.get<bool>("--preserve-root", false);
  cfg.reference_file = ctx.get<std::string>("--reference", "");
  cfg.has_reference = !cfg.reference_file.empty();
  cfg.from_spec = ctx.get<std::string>("--from", "");

  cfg.traverse_cmdline = ctx.get<bool>("-H", false);
  cfg.traverse_all = ctx.get<bool>("-L", false);
  cfg.no_dereference = (ctx.get<bool>("-h", false) ||
                        ctx.get<bool>("--no-dereference", false)) &&
                       !ctx.get<bool>("--dereference", false);

  if (cfg.recursive && ctx.get<bool>("--dereference", false) &&
      !cfg.traverse_cmdline && !cfg.traverse_all) {
    return std::unexpected("-R --dereference requires either -H or -L");
  }
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
    const auto ref_sids = get_sids_for_path(wref);
    cfg.reference_owner_sid = ref_sids.owner;
    cfg.reference_group_sid = ref_sids.group;
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

    if (auto parsed_valid =
            validate_owner_group_spec(parsed, std::string(ctx.positionals[0]));
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

auto process_file(const std::string& path, const Config& cfg, bool no_deref)
    -> int {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD attr = GetFileAttributesW(wpath.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!cfg.quiet) {
      safeErrorPrintLn(
          format_chown_error("cannot access", path, GetLastError()));
    }
    return 1;
  }

  const bool is_link = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  if (is_link && !no_deref) {
    // Following the link: GNU reports "cannot dereference" when the
    // referent does not exist.
    HANDLE probe = CreateFileW(
        wpath.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (probe == INVALID_HANDLE_VALUE) {
      DWORD error = GetLastError();
      if (!cfg.quiet) {
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
          safeErrorPrintLn(
              format_chown_error("cannot dereference", path, error));
        } else {
          safeErrorPrintLn(format_chown_error("cannot access", path, error));
        }
      }
      return 1;
    }
    CloseHandle(probe);
  }

  const auto path_sids =
      no_deref ? get_sids_for_link(wpath) : get_sids_for_path(wpath);

  const bool needs_current_ownership =
      cfg.has_from_spec || cfg.verbose || cfg.changes;
  OwnershipInfo current_ownership;
  if (needs_current_ownership && path_sids.ok) {
    const PSID owner_psid =
        reinterpret_cast<PSID>(const_cast<std::byte*>(path_sids.owner.data()));
    const PSID group_psid =
        reinterpret_cast<PSID>(const_cast<std::byte*>(path_sids.group.data()));
    current_ownership =
        OwnershipInfo{.owner_name = lookup_account_name_from_sid(owner_psid),
                      .owner_id = lookup_account_id_from_sid(owner_psid),
                      .group_name = lookup_account_name_from_sid(group_psid),
                      .group_id = lookup_account_id_from_sid(group_psid)};
  } else if (needs_current_ownership) {
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
    const std::string current_display =
        format_ownership_display(current_ownership);
    if (!cfg.reference_owner_display.empty() &&
        current_display == cfg.reference_owner_display) {
      if (cfg.verbose || cfg.changes) {
        safeErrorPrint("chown: ownership of '");
        safeErrorPrint(path);
        safeErrorPrint("' retained as ");
        safeErrorPrintLn(cfg.reference_owner_display);
      }
      return 0;
    }
  } else if ((cfg.verbose || cfg.changes) &&
             matches_requested_ownership(cfg, current_ownership)) {
    safeErrorPrint("chown: ownership of '");
    safeErrorPrint(path);
    safeErrorPrint("' retained as ");
    safeErrorPrintLn(format_ownership_display(current_ownership).empty()
                         ? std::string("unknown")
                         : format_ownership_display(current_ownership));
    return 0;
  }

  // Resolve the desired owner/group SIDs.
  std::vector<std::byte> owner_sid;
  std::vector<std::byte> group_sid;
  if (cfg.has_reference) {
    owner_sid = cfg.reference_owner_sid;
    group_sid = cfg.reference_group_sid;
  } else {
    if (!cfg.owner.empty()) {
      owner_sid = resolve_account_sid(
          cfg.owner, path_sids.owner.empty()
                         ? nullptr
                         : reinterpret_cast<PSID>(
                               const_cast<std::byte*>(path_sids.owner.data())));
      if (owner_sid.empty()) {
        if (!cfg.quiet) {
          safeErrorPrintLn("chown: invalid user: '" + cfg.owner + "'");
        }
        return 1;
      }
    }
    if (cfg.has_group) {
      if (cfg.group.empty()) {
        // "chown owner:" assigns the owner's login group; the closest
        // Windows equivalent is the caller's primary group.
        group_sid = current_token_sid(TokenPrimaryGroup);
      } else {
        group_sid = resolve_account_sid(
            cfg.group, path_sids.group.empty()
                           ? nullptr
                           : reinterpret_cast<PSID>(const_cast<std::byte*>(
                                 path_sids.group.data())));
      }
      if (group_sid.empty()) {
        if (!cfg.quiet) {
          safeErrorPrintLn("chown: invalid group: '" + cfg.group + "'");
        }
        return 1;
      }
    }
  }

  if (owner_sid.empty() && group_sid.empty()) {
    if (!cfg.quiet) {
      safeErrorPrintLn("chown: invalid spec for '" + path + "'");
    }
    return 1;
  }

  // Elevated privileges let administrators reassign ownership to arbitrary
  // accounts (SeRestorePrivilege) or take ownership (SeTakeOwnership).
  enable_privilege(SE_TAKE_OWNERSHIP_NAME);
  enable_privilege(SE_RESTORE_NAME);

  DWORD open_flags = FILE_FLAG_BACKUP_SEMANTICS;
  if (no_deref) {
    // Open the reparse point itself so -h works on dangling symlinks.
    open_flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }
  HANDLE object =
      CreateFileW(wpath.c_str(), WRITE_OWNER | READ_CONTROL,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, open_flags, nullptr);
  if (object == INVALID_HANDLE_VALUE) {
    if (!cfg.quiet) {
      safeErrorPrintLn(
          format_chown_error("cannot access", path, GetLastError()));
    }
    return 1;
  }

  SECURITY_INFORMATION security_info = 0;
  if (!owner_sid.empty()) security_info |= OWNER_SECURITY_INFORMATION;
  if (!group_sid.empty()) security_info |= GROUP_SECURITY_INFORMATION;
  const DWORD status = SetSecurityInfo(
      object, SE_FILE_OBJECT, security_info,
      owner_sid.empty() ? nullptr : owner_sid.data(),
      group_sid.empty() ? nullptr : group_sid.data(), nullptr, nullptr);
  CloseHandle(object);

  if (status != ERROR_SUCCESS) {
    if (!cfg.quiet) {
      safeErrorPrintLn(
          format_chown_error("changing ownership of", path, status));
    }
    return 1;
  }

  if (cfg.verbose || cfg.changes) {
    const std::string from = format_ownership_display(current_ownership);
    std::string to;
    if (cfg.has_reference) {
      to = cfg.reference_owner_display;
    } else if (cfg.has_group) {
      to = cfg.owner + ":" + cfg.group;
    } else {
      to = cfg.owner;
    }
    safePrint("changed ownership of '" + path + "' from " +
              (from.empty() ? std::string("unknown") : from) + " to " +
              (to.empty() ? std::string("unknown") : to) + "\n");
  }
  return 0;
}

auto process_recursive(const std::string& path, const Config& cfg,
                       bool command_line_arg) -> int {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD attr = GetFileAttributesW(wpath.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!cfg.quiet) {
      safeErrorPrintLn(
          format_chown_error("cannot access", path, GetLastError()));
    }
    return 1;
  }

  const bool link_self =
      cfg.no_dereference ||
      !(cfg.traverse_all || (cfg.traverse_cmdline && command_line_arg));
  int exit_code = process_file(path, cfg, link_self);

  const bool descend =
      (attr & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
      ((attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0 || cfg.traverse_all ||
       (cfg.traverse_cmdline && command_line_arg));
  if (descend) {
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
        int sub_result = process_recursive(subpath, cfg, false);
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
    "Note: On Windows, changing ownership requires the\n"
    "SeTakeOwnership/SeRestore privileges, usually an elevated shell.\n"
    "Use -h to modify a symbolic link itself, including dangling links.",
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
    safeErrorPrint(winux::i18n::translate_error(cfg_result.error()));
    safeErrorPrint("\n");
    safeErrorPrintLn(winux::i18n::format(
        "common.try_help", "Try '{} --help' for more information.", "chown"));
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
      result = process_recursive(file, cfg, true);
    } else {
      result = process_file(file, cfg, cfg.no_dereference);
    }
    if (result != 0) {
      exit_code = result;
    }
  }

  return exit_code;
}
