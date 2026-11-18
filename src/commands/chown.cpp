// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
  std::string reference_owner_name;
  std::string reference_group_name;
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

// GNU splits fatal diagnostics into usage() failures — operand-count and
// option errors, which end with "Try '<cmd> --help' ..." — and error()
// failures (bad owner/group/spec, failed reference stat, -R
// --dereference) which do not (probed WSL 9.4, src/chown.c:271-356).
struct ConfigError {
  std::string message;
  bool usage_error = false;
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

auto sids_equal(const std::vector<std::byte>& a,
                const std::vector<std::byte>& b) -> bool {
  if (a.empty() || b.empty()) return false;
  return EqualSid(const_cast<std::byte*>(a.data()),
                  const_cast<std::byte*>(b.data())) != FALSE;
}

// Fetch owner/group SIDs of the link itself via an OPEN_REPARSE_POINT
// handle; works on dangling symlinks where the named variant follows the
// link and fails.  The operand is folded through the shared boundary first
// (#1241): a raw "/e/..." spelling makes Win32 resolve against the CURRENT
// drive, so the handle opened for "<cwd>:\e\...".
auto get_sids_for_link(const std::wstring& wpath) -> PathSids {
  PathSids result;
  const std::wstring folded = native_path::normalize_api_operand_w(wpath);
  HANDLE handle = CreateFileW(
      folded.c_str(), READ_CONTROL,
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
// component's symbolic link.  The operand is folded through the shared
// boundary first (#1241, same rule as get_sids_for_link).
auto get_sids_for_path(const std::wstring& wpath) -> PathSids {
  PathSids result;
  PSECURITY_DESCRIPTOR security_desc = nullptr;
  PSID owner_sid = nullptr;
  PSID group_sid = nullptr;

  const std::wstring folded = native_path::normalize_api_operand_w(wpath);
  const DWORD status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(folded.c_str()), SE_FILE_OBJECT,
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
    // #1262: a "//e/.."-class operand survives the POSIX two-slash fold and
    // reaches Win32 as an UNC-ish name, which fails at the path-syntax layer
    // (ERROR_BAD_PATHNAME / ERROR_INVALID_NAME) — POSIX name resolution
    // reports it as ENOENT and MSYS prints
    // "cannot access '//e/..': No such file or directory", the same form
    // chgrp already emits (chmod's format_access_error precedent).
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
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

auto get_ownership_info_for_path(const std::string& path) -> OwnershipInfo {
  // #1241: fold the drive dialect before the security lookup (see
  // get_sids_for_path).
  std::wstring wpath =
      utf8_to_wstring(native_path::normalize_api_operand(path));
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

// Which ownership fields the spec covers — the field mask GNU derives
// from user_name/group_name (src/chown.c:285-313): a user field exists
// whenever anything was named (":g" leaves it empty), a group field when
// a group applies, including the "u:" login-group fill.  "" and ":" leave
// uid/gid at -1 so neither field exists (userspec.c:116-133) and the
// chown call is a no-op.
struct SpecFields {
  bool owner = false;
  bool group = false;
};

auto spec_fields(const Config& cfg) -> SpecFields {
  if (cfg.has_reference) {
    return {.owner = true, .group = true};
  }
  return {.owner = !cfg.owner.empty() || !cfg.group.empty(),
          .group = !cfg.group.empty() || (cfg.has_group && !cfg.owner.empty())};
}

auto ownership_name_or_id(const std::string& name, const std::string& id)
    -> std::string {
  return !name.empty() ? name : id;
}

// The old-spec text GNU splices into describe_change (user_group_str on
// the pre-change ids, src/chown-core.c:122-145 + 168-169): only the
// requested fields, ':'-joined; std::nullopt when the spec names no field
// at all ('' or ':'), which selects the "retained" form without "as".
auto old_spec_display(const OwnershipInfo& info, const SpecFields& fields)
    -> std::optional<std::string> {
  if (!fields.owner && !fields.group) {
    return std::nullopt;
  }
  std::string spec;
  if (fields.owner) {
    spec = ownership_name_or_id(info.owner_name, info.owner_id);
  }
  if (fields.group) {
    spec += ":" + ownership_name_or_id(info.group_name, info.group_id);
  }
  return spec;
}

// The new-spec text GNU shows in describe_change (user_name/group_name
// parse outputs or --reference names): the typed owner, ':' plus the
// typed/resolved group when a group field exists.
auto new_spec_display(const Config& cfg, const SpecFields& fields)
    -> std::string {
  if (cfg.has_reference) {
    return cfg.reference_owner_name + ":" + cfg.reference_group_name;
  }
  std::string spec = cfg.owner;
  if (fields.group) {
    spec += ":";
    if (!cfg.group.empty()) {
      spec += cfg.group;
    } else {
      // "u:" picks the owner's login group; the closest Windows
      // equivalent is the caller's primary group.
      const auto primary = current_token_sid(TokenPrimaryGroup);
      const PSID psid =
          primary.empty()
              ? nullptr
              : reinterpret_cast<PSID>(const_cast<std::byte*>(primary.data()));
      spec += ownership_name_or_id(lookup_account_name_from_sid(psid),
                                   lookup_account_id_from_sid(psid));
    }
  }
  return spec;
}

enum class OwnershipChange { retained, changed, failed };

// GNU describe_change (src/chown-core.c:151-208): the reports go to
// stdout via printf and carry no program-name prefix.  -v (V_high)
// prints every outcome, -c (V_changes_only) only real changes — call
// sites gate on the verbosity flags, this helper only formats.
auto describe_change(const std::string& path, OwnershipChange change,
                     const std::optional<std::string>& old_spec,
                     const std::string& new_spec) -> void {
  switch (change) {
    case OwnershipChange::retained:
      if (old_spec.has_value()) {
        safePrintLn(winux::i18n::format("command.chown.report.retained_as",
                                        "ownership of '{}' retained as {}",
                                        path, *old_spec));
      } else {
        safePrintLn(winux::i18n::format("command.chown.report.retained",
                                        "ownership of '{}' retained", path));
      }
      break;
    case OwnershipChange::changed:
      safePrintLn(winux::i18n::format("command.chown.report.changed",
                                      "changed ownership of '{}' from {} to {}",
                                      path, old_spec.value_or(std::string{}),
                                      new_spec));
      break;
    case OwnershipChange::failed:
      if (new_spec.empty()) {
        // '' or ':' names no field — GNU prints the bare form.
        safePrintLn(winux::i18n::format("command.chown.report.failed",
                                        "failed to change ownership of '{}'",
                                        path));
      } else if (old_spec.has_value()) {
        safePrintLn(winux::i18n::format(
            "command.chown.report.failed_from_to",
            "failed to change ownership of '{}' from {} to {}", path, *old_spec,
            new_spec));
      } else {
        safePrintLn(winux::i18n::format(
            "command.chown.report.failed_to",
            "failed to change ownership of '{}' to {}", path, new_spec));
      }
      break;
  }
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

// True when an absolute operand lexically collapses onto the root: the
// "."/".." reduction consumes every component ("/e/..", "/tmp/..",
// "///e/../..").  POSIX keeps "//"-prefixed operands rooted at "//" (MSYS
// resolves "//e/.." against the "//e" name, never "/"), so exactly two
// leading separators followed by a component opt out.
auto collapses_onto_root(const std::string& path) -> bool {
  if (path.empty()) return false;
  if (path.front() != '/' && path.front() != '\\') return false;
  size_t leading_separators = 0;
  while (
      leading_separators < path.size() &&
      (path[leading_separators] == '/' || path[leading_separators] == '\\')) {
    ++leading_separators;
  }
  if (leading_separators == 2 && leading_separators < path.size()) {
    return false;
  }
  std::vector<std::string_view> stack;
  size_t i = 0;
  while (i < path.size()) {
    while (i < path.size() && (path[i] == '/' || path[i] == '\\')) {
      ++i;
    }
    const size_t start = i;
    while (i < path.size() && path[i] != '/' && path[i] != '\\') {
      ++i;
    }
    if (start == i) continue;
    const std::string_view component(path.data() + start, i - start);
    if (component == ".") continue;
    if (component == "..") {
      if (!stack.empty()) stack.pop_back();
      continue;
    }
    stack.push_back(component);
  }
  return stack.empty();
}

auto get_preserve_root_match(const std::string& input_path)
    -> std::optional<PreserveRootMatch> {
  // #1241: fold the drive dialect first — "/e" names E:\, and an unfolded
  // GetFullPathNameW resolved it against the CURRENT drive, so the
  // --preserve-root failsafe compared "<cwd>:\e" against the volume root and
  // missed (chgrp twin fix).
  //
  // All-separator spellings ("//", "///") never reach that fold: the operand
  // layer collapses them onto "/" (then through the shell tree), while a
  // verbatim GetFullPathNameW("//") answers for an UNC-ish name — keeping
  // them verbatim let `chown -R --preserve-root X //` bypass the failsafe
  // and really walk the shell root (CR on #1249).  They hit the root match
  // directly, printing the collapsed spelling like MSYS/GNU ("on '/'",
  // "on '//'").  A spelling that lexically collapses onto the root fires
  // with the "(same as '/')" suffix, exactly like MSYS for "/e/..".
  if (!input_path.empty() &&
      input_path.find_first_not_of("/\\") == std::string::npos) {
    return PreserveRootMatch{input_path == "//" ? "//" : "/", false};
  }
  if (collapses_onto_root(input_path)) {
    return PreserveRootMatch{input_path, true};
  }
  const std::string folded = native_path::normalize_api_operand(input_path);
  std::wstring full_path = get_full_path(utf8_to_wstring(folded));
  if (full_path.empty()) {
    return std::nullopt;
  }

  const auto same_directory = [](std::wstring_view a, std::wstring_view b) {
    return normalize_path_for_root_compare(
               std::wstring(native_path::strip_trailing_separators(a))) ==
           normalize_path_for_root_compare(
               std::wstring(native_path::strip_trailing_separators(b)));
  };

  std::wstring volume_root = get_volume_root_path(full_path);
  if (!volume_root.empty() && same_directory(full_path, volume_root)) {
    return PreserveRootMatch{input_path, input_path != "/"};
  }

  // #1259: GNU compares the operand's device+inode against "/", so a
  // relative spelling whose resolved directory IS the root is refused like
  // "/" - ".", "./" and "././" from a cwd sitting on the shell root used to
  // walk the shell tree because the volume-root probe above only catches
  // spellings that name the volume root.  Resolving "/" through the same
  // operand layer names the shell root when one is configured (the
  // current-drive root otherwise), which is the comparison target.
  const std::wstring root_full =
      get_full_path(utf8_to_wstring(native_path::normalize_api_operand("/")));
  if (!root_full.empty() && same_directory(full_path, root_full)) {
    return PreserveRootMatch{input_path, input_path != "/"};
  }

  return std::nullopt;
}

auto add_file_args(Config& cfg, std::span<const std::string_view> args)
    -> void {
  for (auto arg : args) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      // #1241: enumerate the folded spelling so the glob matches on the
      // operand's drive instead of "<cwd>:\e\..."; an unmatched pattern
      // stays literal below so diagnostics quote what the user typed.
      auto glob_result =
          glob_expand(native_path::normalize_api_operand(file_arg));
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

// [GNU] lib/userspec.c parse_with_separator: each side of "[owner][:.]
// [group]" must name a resolvable account or a bare numeric id; ':' inside
// the group half stays literal.  An unresolvable owner followed by a
// separator but no group text is "invalid spec" (the "u:" login-group
// form), checked before the numeric fallback.
enum class SpecError { none, invalid_user, invalid_group, invalid_spec };

// Windows resolves "." to a local account where POSIX getpwnam/getgrnam
// always fail; keep the GNU verdict or "chown .." would warn and succeed.
auto spec_name_resolves(const std::string& name) -> bool {
  return name != "." && lookup_account_type(name).has_value();
}

auto validate_spec_fields(const std::string& owner, const std::string& group,
                          bool has_group) -> SpecError {
  if (!owner.empty() && !spec_name_resolves(owner)) {
    if (has_group && group.empty()) {
      return SpecError::invalid_spec;
    }
    if (!is_numeric_id(owner)) {
      return SpecError::invalid_user;
    }
  }
  if (has_group && !group.empty() && !spec_name_resolves(group) &&
      !is_numeric_id(group)) {
    return SpecError::invalid_group;
  }
  return SpecError::none;
}

auto spec_error_message(SpecError error, const std::string& raw_spec)
    -> std::string {
  switch (error) {
    case SpecError::invalid_user:
      return "invalid user: '" + raw_spec + "'";
    case SpecError::invalid_group:
      return "invalid group: '" + raw_spec + "'";
    case SpecError::invalid_spec:
      return "invalid spec: '" + raw_spec + "'";
    default:
      return {};
  }
}

// [GNU] userspec.c parse_user_spec_warn: ':' is the separator.  A spec
// without ':' that fails to resolve as a user is retried with '.' as the
// (deprecated) separator — but only when that split parses cleanly, in
// which case the caller owes the "'.' should be ':'" warning.  A failed
// retry reports the ORIGINAL "invalid user" error with no warning.
auto parse_owner_group_spec(const std::string& spec)
    -> cp::Result<OwnerGroupSpec> {
  OwnerGroupSpec parsed;
  const size_t colon = spec.find(':');
  if (colon != std::string::npos) {
    parsed.owner = spec.substr(0, colon);
    parsed.group = spec.substr(colon + 1);
    parsed.has_group = true;
  } else {
    parsed.owner = spec;
  }

  const SpecError error =
      validate_spec_fields(parsed.owner, parsed.group, parsed.has_group);
  if (error == SpecError::none) {
    return parsed;
  }

  if (colon == std::string::npos) {
    if (const size_t dot = spec.find('.'); dot != std::string::npos) {
      OwnerGroupSpec dotted;
      dotted.owner = spec.substr(0, dot);
      dotted.group = spec.substr(dot + 1);
      dotted.has_group = true;
      dotted.warned_dot_separator = true;
      if (validate_spec_fields(dotted.owner, dotted.group,
                               dotted.has_group) == SpecError::none) {
        return dotted;
      }
    }
  }

  return std::unexpected(spec_error_message(error, spec));
}

auto emit_warning(const std::string& message) -> void {
  safeErrorPrint("chown: warning: ");
  safeErrorPrint(message);
  safeErrorPrint("\n");
}

auto build_config(const CommandContext<CHOWN_OPTIONS.size()>& ctx)
    -> std::expected<Config, ConfigError> {
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

  // [GNU] chown.c:278-288 reports this through error(EXIT_FAILURE) — no
  // "Try --help" line — and checks it before the operand count.
  if (cfg.recursive && ctx.get<bool>("--dereference", false) &&
      !cfg.traverse_cmdline && !cfg.traverse_all) {
    return std::unexpected(
        ConfigError{"-R --dereference requires either -H or -L"});
  }
  (void)ctx.get<bool>("-P", false);
  (void)ctx.get<bool>("--no-preserve-root", false);

  // [GNU] --from is parsed inside the getopt loop (chown.c:245-253), so
  // its spec errors fire before the operand-count check below.
  if (!cfg.from_spec.empty()) {
    auto from = parse_owner_group_spec(cfg.from_spec);
    if (!from) {
      return std::unexpected(ConfigError{from.error()});
    }
    if (from->warned_dot_separator) {
      emit_warning("'.' should be ':': '" + cfg.from_spec + "'");
    }
    cfg.from_owner_group = *from;
    cfg.has_from_spec = true;
  }

  // [GNU] chown.c:295-302 checks the operand count before the reference
  // stat and before the spec parse; only these count errors (and getopt
  // failures) carry the "Try --help" line.
  if (ctx.positionals.empty()) {
    return std::unexpected(ConfigError{"missing operand", true});
  }
  if (!cfg.has_reference && ctx.positionals.size() < 2) {
    return std::unexpected(ConfigError{
        "missing operand after '" + std::string(ctx.positionals[0]) + "'",
        true});
  }

  if (cfg.has_reference) {
    // #1241: fold the drive dialect before the reference probe; the error
    // keeps the user's spelling (display strings ride alongside API forms).
    std::wstring wref =
        utf8_to_wstring(native_path::normalize_api_operand(cfg.reference_file));
    if (GetFileAttributesW(wref.c_str()) == INVALID_FILE_ATTRIBUTES) {
      return std::unexpected(ConfigError{
          format_missing_reference_error(cfg.reference_file, GetLastError())});
    }
    const auto ref_sids = get_sids_for_path(wref);
    cfg.reference_owner_sid = ref_sids.owner;
    cfg.reference_group_sid = ref_sids.group;
    const PSID ref_owner_psid =
        ref_sids.owner.empty() ? nullptr
                               : reinterpret_cast<PSID>(const_cast<std::byte*>(
                                     ref_sids.owner.data()));
    const PSID ref_group_psid =
        ref_sids.group.empty() ? nullptr
                               : reinterpret_cast<PSID>(const_cast<std::byte*>(
                                     ref_sids.group.data()));
    cfg.reference_owner_name =
        ownership_name_or_id(lookup_account_name_from_sid(ref_owner_psid),
                             lookup_account_id_from_sid(ref_owner_psid));
    cfg.reference_group_name =
        ownership_name_or_id(lookup_account_name_from_sid(ref_group_psid),
                             lookup_account_id_from_sid(ref_group_psid));
    add_file_args(cfg, std::span<const std::string_view>(
                           ctx.positionals.data(), ctx.positionals.size()));
  } else {
    const std::string spec_text(ctx.positionals[0]);
    auto parsed = parse_owner_group_spec(spec_text);
    if (!parsed) {
      return std::unexpected(ConfigError{parsed.error()});
    }
    if (parsed->warned_dot_separator) {
      emit_warning("'.' should be ':': '" + spec_text + "'");
    }
    cfg.owner = parsed->owner;
    cfg.group = parsed->group;
    cfg.has_group = parsed->has_group;

    add_file_args(
        cfg, std::span<const std::string_view>(ctx.positionals.data() + 1,
                                               ctx.positionals.size() - 1));
  }

  return cfg;
}

auto process_file(const std::string& path, const Config& cfg, bool no_deref)
    -> int {
  // #1241: the operand may use any accepted POSIX drive spelling.  Fold once
  // through the shared make_api_path_operand boundary before any Win32 call;
  // an unfolded spelling made every probe and the WRITE_OWNER open answer for
  // "<cwd>:\e\..." (chgrp/mkdir precedent).  `path` stays verbatim in every
  // message below.
  const auto operand = native_path::make_api_path_operand(path);
  const std::wstring& wpath = operand.extended;

  const SpecFields fields = spec_fields(cfg);
  // '' and ':' name no field at all (userspec.c:116-133 leaves uid/gid
  // -1), so the operation is a no-op the kernel performs for any caller.
  const bool desired_owner = cfg.has_reference || !cfg.owner.empty();
  const bool desired_group = cfg.has_reference || fields.group;
  const std::string new_spec = new_spec_display(cfg, fields);

  // -v reports a failed change on stdout too (GNU describe_change with
  // CH_FAILED, src/chown-core.c:181-193); the old spec exists only when
  // the pre-change ids could be read.
  auto describe_failed = [&](const OwnershipInfo* info) {
    if (cfg.verbose) {
      describe_change(
          path, OwnershipChange::failed,
          info != nullptr ? old_spec_display(*info, fields) : std::nullopt,
          new_spec);
    }
  };

  DWORD attr = GetFileAttributesW(wpath.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!cfg.quiet) {
      safeErrorPrintLn(
          format_chown_error("cannot access", path, GetLastError()));
    }
    describe_failed(nullptr);
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
      // GNU's CH_FAILED line shows the link's own (lstat) ids.
      const auto link_sids = get_sids_for_link(operand.normalized);
      OwnershipInfo link_ownership;
      if (link_sids.ok) {
        const PSID link_owner = reinterpret_cast<PSID>(
            const_cast<std::byte*>(link_sids.owner.data()));
        const PSID link_group = reinterpret_cast<PSID>(
            const_cast<std::byte*>(link_sids.group.data()));
        link_ownership = OwnershipInfo{
            .owner_name = lookup_account_name_from_sid(link_owner),
            .owner_id = lookup_account_id_from_sid(link_owner),
            .group_name = lookup_account_name_from_sid(link_group),
            .group_id = lookup_account_id_from_sid(link_group)};
      }
      describe_failed(link_sids.ok ? &link_ownership : nullptr);
      return 1;
    }
    CloseHandle(probe);
  }

  // The security helpers re-fold their operand: hand them the native
  // (non-extended) spelling so an already-folded form never re-enters the
  // fold channel.
  const auto path_sids = no_deref ? get_sids_for_link(operand.normalized)
                                  : get_sids_for_path(operand.normalized);

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
      describe_change(path, OwnershipChange::retained,
                      old_spec_display(current_ownership, fields), new_spec);
    }
    return 0;
  }

  if (!desired_owner && !desired_group) {
    if (cfg.verbose) {
      describe_change(path, OwnershipChange::retained, std::nullopt, {});
    }
    return 0;
  }

  // Resolve the desired owner/group SIDs.
  std::vector<std::byte> owner_sid;
  std::vector<std::byte> group_sid;
  if (cfg.has_reference) {
    owner_sid = cfg.reference_owner_sid;
    group_sid = cfg.reference_group_sid;
  } else {
    if (desired_owner) {
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
    if (desired_group) {
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

  // chown_ok/chgrp_ok let the file's owner run a change onto the current
  // uid/gid without privilege; anyone else gets EPERM.  Skip the
  // privileged write only when every requested field already matches and
  // the caller IS the file owner — a matching request from a different
  // account still has to take the failing path below.
  const bool unchanged =
      path_sids.ok &&
      (!desired_owner || sids_equal(owner_sid, path_sids.owner)) &&
      (!desired_group || sids_equal(group_sid, path_sids.group));
  if (unchanged) {
    const auto token_user = current_token_sid(TokenUser);
    if (!token_user.empty() && sids_equal(token_user, path_sids.owner)) {
      if (cfg.verbose) {
        describe_change(path, OwnershipChange::retained,
                        old_spec_display(current_ownership, fields), new_spec);
      }
      return 0;
    }
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
  // GNU chown-core.c:481-486 reports the failed change, not an access
  // probe — "changing group of" when only a group was requested.
  const char* change_verb =
      desired_owner ? "changing ownership of" : "changing group of";
  if (object == INVALID_HANDLE_VALUE) {
    if (!cfg.quiet) {
      safeErrorPrintLn(format_chown_error(change_verb, path, GetLastError()));
    }
    describe_failed(path_sids.ok ? &current_ownership : nullptr);
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
      safeErrorPrintLn(format_chown_error(change_verb, path, status));
    }
    describe_failed(path_sids.ok ? &current_ownership : nullptr);
    return 1;
  }

  if (unchanged) {
    // A privileged caller ran the no-op for a file it does not own; GNU
    // still counts it as CH_NO_CHANGE_REQUESTED (-v only).
    if (cfg.verbose) {
      describe_change(path, OwnershipChange::retained,
                      old_spec_display(current_ownership, fields), new_spec);
    }
  } else if (cfg.verbose || cfg.changes) {
    describe_change(path, OwnershipChange::changed,
                    old_spec_display(current_ownership, fields), new_spec);
  }
  return 0;
}

auto process_recursive(const std::string& path, const Config& cfg,
                       bool command_line_arg) -> int {
  // #1241: fold for the enumeration/attribute APIs; children re-fold from
  // their own display spelling (chmod -R precedent).
  const auto operand = native_path::make_api_path_operand(path);
  const std::wstring& wpath = operand.extended;

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

        // [GNU] fts composes child names with '/'; the display spelling
        // keeps it (Win32 accepts '/' in the folded API operand).
        std::string subpath = path + "/" + wstring_to_utf8(filename);
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
    safeErrorPrint(
        winux::i18n::translate_error(cfg_result.error().message));
    safeErrorPrint("\n");
    if (cfg_result.error().usage_error) {
      safeErrorPrintLn(winux::i18n::format(
          "common.try_help", "Try '{} --help' for more information.", "chown"));
    }
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
