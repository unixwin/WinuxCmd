/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */
module;

// clang-format off: pch.h must include winsock2.h before Windows headers.
#include "pch/pch.h"

#include <aclapi.h>
// clang-format on

export module utils:win32;

import std;
import :utf8;

export class UniqueHandle {
 public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE handle) : handle_(handle) {}

  UniqueHandle(const UniqueHandle&) = delete;
  auto operator=(const UniqueHandle&) -> UniqueHandle& = delete;

  UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}

  auto operator=(UniqueHandle&& other) noexcept -> UniqueHandle& {
    if (this != &other) reset(other.release());
    return *this;
  }

  ~UniqueHandle() { reset(); }

  [[nodiscard]] auto get() const noexcept -> HANDLE { return handle_; }

  [[nodiscard]] auto valid() const noexcept -> bool {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }

  explicit operator bool() const noexcept { return valid(); }

  auto release() noexcept -> HANDLE {
    HANDLE handle = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return handle;
  }

  auto reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept -> void {
    if (valid()) CloseHandle(handle_);
    handle_ = handle;
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

export class UniqueFindHandle {
 public:
  UniqueFindHandle() = default;
  explicit UniqueFindHandle(HANDLE handle) : handle_(handle) {}

  UniqueFindHandle(const UniqueFindHandle&) = delete;
  auto operator=(const UniqueFindHandle&) -> UniqueFindHandle& = delete;

  UniqueFindHandle(UniqueFindHandle&& other) noexcept
      : handle_(other.release()) {}

  auto operator=(UniqueFindHandle&& other) noexcept -> UniqueFindHandle& {
    if (this != &other) reset(other.release());
    return *this;
  }

  ~UniqueFindHandle() { reset(); }

  [[nodiscard]] auto get() const noexcept -> HANDLE { return handle_; }

  [[nodiscard]] auto valid() const noexcept -> bool {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }

  explicit operator bool() const noexcept { return valid(); }

  auto release() noexcept -> HANDLE {
    HANDLE handle = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return handle;
  }

  auto reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept -> void {
    if (valid()) FindClose(handle_);
    handle_ = handle;
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

export auto quote_windows_command_arg(std::wstring_view arg) -> std::wstring {
  if (arg.empty()) return L"\"\"";

  const bool need_quote =
      arg.find_first_of(L" \t\"") != std::wstring_view::npos;
  if (!need_quote) return std::wstring(arg);

  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      backslashes = 0;
      out.push_back(c);
    }
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

export auto append_windows_command_arg(std::wstring& command_line,
                                       std::wstring_view arg) -> void {
  if (!command_line.empty()) command_line.push_back(L' ');
  command_line += quote_windows_command_arg(arg);
}

export auto build_windows_command_line(std::span<const std::string_view> args)
    -> std::wstring {
  std::wstring out;
  for (auto arg : args) {
    append_windows_command_arg(out, utf8_to_wstring(std::string(arg)));
  }
  return out;
}

export struct Win32ErrorTextOptions {
  bool file_exists = false;
  bool bad_exe_format_as_permission = false;
  bool privilege_not_held_as_not_permitted = false;
  bool invalid_name_as_missing = false;
};

export auto win32_posix_error_text(unsigned long error,
                                   Win32ErrorTextOptions options = {})
    -> std::string {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return "No such file or directory";
    case ERROR_INVALID_NAME:
      if (options.invalid_name_as_missing) return "No such file or directory";
      break;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      if (options.file_exists) return "File exists";
      break;
    case ERROR_ACCESS_DENIED:
      return "Permission denied";
    case ERROR_DIRECTORY:
      return "Not a directory";
    case ERROR_BAD_EXE_FORMAT:
      if (options.bad_exe_format_as_permission) return "Permission denied";
      break;
    case ERROR_PRIVILEGE_NOT_HELD:
      if (options.privilege_not_held_as_not_permitted) {
        return "Operation not permitted";
      }
      break;
    case ERROR_INVALID_PARAMETER:
      return "Invalid argument";
    default:
      break;
  }
  return std::system_category().message(static_cast<int>(error));
}

export auto win32_system_error_text(unsigned long error) -> std::string {
  LPWSTR buffer = nullptr;
  DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                  FORMAT_MESSAGE_FROM_SYSTEM |
                                  FORMAT_MESSAGE_IGNORE_INSERTS,
                              nullptr, static_cast<DWORD>(error), 0,
                              reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  if (size == 0 || buffer == nullptr) {
    return std::system_category().message(static_cast<int>(error));
  }

  std::wstring text(buffer, size);
  LocalFree(buffer);
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
                           text.back() == L' ' || text.back() == L'\t')) {
    text.pop_back();
  }
  return wstring_to_utf8(text);
}

export struct Win32ProcessInfo {
  DWORD pid = 0;
  DWORD ppid = 0;
  LONG priority_base = 0;
  std::wstring name;
  std::wstring command_line;
};

export struct Win32ModuleInfo {
  std::wstring name;
  std::wstring path;
};

export struct Win32AccountInfo {
  std::string id;
  std::string name;
};

export auto win32_parse_pid(std::string_view text) -> std::optional<DWORD> {
  unsigned long value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size() || value == 0) {
    return std::nullopt;
  }
  return static_cast<DWORD>(value);
}

export auto win32_current_username() -> std::string {
  WCHAR username[256];
  DWORD username_size = 256;
  if (!GetUserNameW(username, &username_size)) return {};
  return wstring_to_utf8(std::wstring(username));
}

export auto win32_sid_authority_value(PSID sid) -> unsigned long long {
  SID_IDENTIFIER_AUTHORITY* authority = GetSidIdentifierAuthority(sid);
  if (authority == nullptr) return 0;

  unsigned long long value = 0;
  for (unsigned char byte : authority->Value) {
    value = (value << 8) | byte;
  }
  return value;
}

export auto win32_account_name_from_sid(PSID sid) -> std::string {
  if (sid == nullptr) return {};

  DWORD name_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type = SidTypeUnknown;
  LookupAccountSidW(nullptr, sid, nullptr, &name_size, nullptr, &domain_size,
                    &sid_type);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return {};

  std::wstring name(name_size, wchar_t{});
  std::wstring domain(domain_size, wchar_t{});
  if (!LookupAccountSidW(nullptr, sid, name.data(), &name_size, domain.data(),
                         &domain_size, &sid_type)) {
    return {};
  }

  name.resize(name_size);
  return wstring_to_utf8(name);
}

export auto win32_account_id_from_sid(PSID sid) -> std::string {
  if (sid == nullptr || !IsValidSid(sid)) return {};

  PUCHAR subauth_count = GetSidSubAuthorityCount(sid);
  if (subauth_count == nullptr || *subauth_count == 0) return {};

  DWORD* rid = GetSidSubAuthority(sid, *subauth_count - 1);
  if (rid == nullptr) return {};

  const unsigned long long authority = win32_sid_authority_value(sid);
  DWORD* first = GetSidSubAuthority(sid, 0);
  const DWORD first_subauth = first == nullptr ? 0 : *first;

  if (authority == 5 && first_subauth == 21 && *subauth_count >= 5) {
    return std::to_string(0x30000u + *rid);
  }
  if (authority == 16) {
    return std::to_string(0x60000u + *rid);
  }

  return std::to_string(*rid);
}

export auto win32_account_from_sid(PSID sid) -> Win32AccountInfo {
  Win32AccountInfo account{.id = win32_account_id_from_sid(sid),
                           .name = win32_account_name_from_sid(sid)};
  if (account.id.empty()) account.id = "0";
  return account;
}

export struct Win32FileAccounts {
  Win32AccountInfo owner;
  Win32AccountInfo group;
};

export auto win32_file_accounts(std::wstring_view path) -> Win32FileAccounts {
  PSID owner_sid = nullptr;
  PSID group_sid = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const auto status = GetNamedSecurityInfoW(
      const_cast<wchar_t*>(path.data()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, &owner_sid,
      &group_sid, nullptr, nullptr, &descriptor);
  if (status != ERROR_SUCCESS) return {};

  Win32FileAccounts accounts;
  if (owner_sid) accounts.owner = win32_account_from_sid(owner_sid);
  if (group_sid) accounts.group = win32_account_from_sid(group_sid);
  if (descriptor) LocalFree(descriptor);
  return accounts;
}

export auto win32_lookup_account(std::wstring_view name)
    -> std::optional<Win32AccountInfo> {
  DWORD sid_size = 0;
  DWORD domain_size = 0;
  SID_NAME_USE sid_type = SidTypeUnknown;
  std::wstring wname(name);
  LookupAccountNameW(nullptr, wname.c_str(), nullptr, &sid_size, nullptr,
                     &domain_size, &sid_type);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return std::nullopt;

  std::vector<std::byte> sid_buffer(sid_size);
  std::wstring domain(domain_size, L'\0');
  if (!LookupAccountNameW(nullptr, wname.c_str(), sid_buffer.data(), &sid_size,
                          domain.data(), &domain_size, &sid_type)) {
    return std::nullopt;
  }

  auto account = win32_account_from_sid(sid_buffer.data());
  if (account.name.empty()) account.name = wstring_to_utf8(name);
  return account;
}

// GNU userspec resolves account names before interpreting decimal IDs.
export auto win32_account_matches(std::string_view spec, std::string_view name,
                                  std::string_view id) -> bool {
  if (auto account = win32_lookup_account(utf8_to_wstring(spec))) {
    const auto expected = utf8_to_wstring(account->name);
    const auto actual = utf8_to_wstring(name);
    return CompareStringOrdinal(expected.data(),
                                static_cast<int>(expected.size()),
                                actual.data(), static_cast<int>(actual.size()),
                                TRUE) == CSTR_EQUAL;
  }
  unsigned long long numeric = 0;
  const auto [end, error] =
      std::from_chars(spec.data(), spec.data() + spec.size(), numeric);
  return error == std::errc{} && end == spec.data() + spec.size() &&
         std::to_string(numeric) == id;
}

export auto win32_token_information(HANDLE token,
                                    TOKEN_INFORMATION_CLASS token_class)
    -> std::vector<std::byte> {
  DWORD size = 0;
  GetTokenInformation(token, token_class, nullptr, 0, &size);
  if (size == 0) return {};

  std::vector<std::byte> data(size);
  if (!GetTokenInformation(token, token_class, data.data(), size, &size)) {
    return {};
  }
  return data;
}

export auto win32_current_token_accounts()
    -> std::expected<std::vector<Win32AccountInfo>, std::string> {
  HANDLE raw_token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
    return std::unexpected(win32_posix_error_text(GetLastError()));
  }
  UniqueHandle token(raw_token);

  std::vector<Win32AccountInfo> accounts;
  auto user_data = win32_token_information(token.get(), TokenUser);
  if (!user_data.empty()) {
    auto* token_user = reinterpret_cast<TOKEN_USER*>(user_data.data());
    auto user = win32_account_from_sid(token_user->User.Sid);
    if (user.name.empty()) user.name = win32_current_username();
    accounts.push_back(std::move(user));
  }

  auto groups_data = win32_token_information(token.get(), TokenGroups);
  if (!groups_data.empty()) {
    auto* token_groups = reinterpret_cast<TOKEN_GROUPS*>(groups_data.data());
    for (DWORD i = 0; i < token_groups->GroupCount; ++i) {
      Win32AccountInfo group =
          win32_account_from_sid(token_groups->Groups[i].Sid);
      if (group.id.empty()) continue;
      bool duplicate = false;
      for (const auto& existing : accounts) {
        if (existing.id == group.id) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) accounts.push_back(std::move(group));
    }
  }

  if (accounts.empty()) {
    return std::unexpected("cannot query process token accounts");
  }
  return accounts;
}

export auto win32_basename_without_exe(std::wstring value) -> std::wstring {
  const auto slash = value.find_last_of(L"\\/");
  if (slash != std::wstring::npos) value.erase(0, slash + 1);
  if (value.size() > 4) {
    std::wstring suffix = value.substr(value.size() - 4);
    for (auto& ch : suffix) {
      if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    if (suffix == L".exe") value.resize(value.size() - 4);
  }
  return value;
}

export auto win32_process_command_line(DWORD pid) -> std::wstring {
  UniqueHandle process(OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid));
  if (!process) return L"";

  PROCESS_BASIC_INFORMATION pbi = {};
  using NtQueryInformationProcessFn =
      NTSTATUS(WINAPI*)(HANDLE, DWORD, PVOID, ULONG, PULONG);

  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) return L"";

  auto query = reinterpret_cast<NtQueryInformationProcessFn>(
      GetProcAddress(ntdll, "NtQueryInformationProcess"));
  if (!query) return L"";

  ULONG len = 0;
  if (query(process.get(), 0, &pbi, sizeof(pbi), &len) != 0 ||
      !pbi.PebBaseAddress) {
    return L"";
  }

  SIZE_T read = 0;
  PVOID params = nullptr;
  if (!ReadProcessMemory(process.get(),
                         reinterpret_cast<PBYTE>(pbi.PebBaseAddress) + 0x20,
                         &params, sizeof(params), &read) ||
      read != sizeof(params) || !params) {
    return L"";
  }

  UNICODE_STRING cmd_line = {};
  if (!ReadProcessMemory(process.get(), static_cast<PBYTE>(params) + 0x70,
                         &cmd_line, sizeof(cmd_line), &read) ||
      read != sizeof(cmd_line) || cmd_line.Length == 0 || !cmd_line.Buffer ||
      cmd_line.Length > 32768) {
    return L"";
  }

  std::vector<wchar_t> buffer(cmd_line.Length / sizeof(wchar_t) + 1);
  if (!ReadProcessMemory(process.get(), cmd_line.Buffer, buffer.data(),
                         cmd_line.Length, &read)) {
    return L"";
  }
  return std::wstring(buffer.data(), cmd_line.Length / sizeof(wchar_t));
}

export auto enumerate_win32_processes(bool include_command_lines = true)
    -> std::vector<Win32ProcessInfo> {
  std::vector<Win32ProcessInfo> out;
  UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot) return out;

  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (!Process32FirstW(snapshot.get(), &pe)) return out;

  do {
    Win32ProcessInfo info;
    info.pid = pe.th32ProcessID;
    info.ppid = pe.th32ParentProcessID;
    info.priority_base = pe.pcPriClassBase;
    info.name = pe.szExeFile;
    if (include_command_lines && info.pid != 0 && info.pid != 4) {
      info.command_line = win32_process_command_line(info.pid);
    }
    if (info.command_line.empty()) info.command_line = info.name;
    out.push_back(std::move(info));
  } while (Process32NextW(snapshot.get(), &pe));

  return out;
}

export auto enumerate_win32_modules(DWORD pid)
    -> std::expected<std::vector<Win32ModuleInfo>, std::string> {
  UniqueHandle snapshot(
      CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
  if (!snapshot) {
    DWORD error = GetLastError();
    return std::unexpected(win32_posix_error_text(error));
  }

  MODULEENTRY32W me{};
  me.dwSize = sizeof(me);
  if (!Module32FirstW(snapshot.get(), &me)) {
    DWORD error = GetLastError();
    return std::unexpected(win32_posix_error_text(error));
  }

  std::vector<Win32ModuleInfo> modules;
  do {
    modules.push_back(Win32ModuleInfo{me.szModule, me.szExePath});
  } while (Module32NextW(snapshot.get(), &me));

  return modules;
}

export auto win32_niceness_from_priority_class(DWORD priority_class) -> int {
  switch (priority_class) {
    case HIGH_PRIORITY_CLASS:
      return -10;
    case ABOVE_NORMAL_PRIORITY_CLASS:
      return -5;
    case BELOW_NORMAL_PRIORITY_CLASS:
      return 10;
    case IDLE_PRIORITY_CLASS:
      return 19;
    case NORMAL_PRIORITY_CLASS:
    default:
      return 0;
  }
}

export auto win32_current_niceness() -> int {
  return win32_niceness_from_priority_class(
      GetPriorityClass(GetCurrentProcess()));
}

export auto win32_priority_class_for_niceness(int niceness) -> DWORD {
  if (niceness <= -10) return HIGH_PRIORITY_CLASS;
  if (niceness < 0) return ABOVE_NORMAL_PRIORITY_CLASS;
  if (niceness >= 19) return IDLE_PRIORITY_CLASS;
  if (niceness >= 10) return BELOW_NORMAL_PRIORITY_CLASS;
  return NORMAL_PRIORITY_CLASS;
}

export struct Win32ProcessActionResult {
  bool ok = false;
  DWORD error = 0;
  std::string message;
};

export auto win32_check_process(DWORD pid) -> Win32ProcessActionResult {
  UniqueHandle process(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process) {
    DWORD error = GetLastError();
    return {false, error, win32_posix_error_text(error)};
  }

  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process.get(), &exit_code)) {
    DWORD error = GetLastError();
    return {false, error, win32_posix_error_text(error)};
  }
  if (exit_code != STILL_ACTIVE) {
    return {false, ERROR_INVALID_PARAMETER, "process already exited"};
  }
  return {true, 0, {}};
}

export auto win32_terminate_process(DWORD pid) -> Win32ProcessActionResult {
  UniqueHandle process(OpenProcess(
      PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
      FALSE, pid));
  if (!process) {
    DWORD error = GetLastError();
    return {false, error, win32_posix_error_text(error)};
  }

  DWORD exit_code = 0;
  if (GetExitCodeProcess(process.get(), &exit_code) &&
      exit_code != STILL_ACTIVE) {
    return {false, ERROR_INVALID_PARAMETER, "process already exited"};
  }

  if (!TerminateProcess(process.get(), 1)) {
    DWORD error = GetLastError();
    return {false, error, win32_posix_error_text(error)};
  }
  WaitForSingleObject(process.get(), 5000);
  return {true, 0, {}};
}
