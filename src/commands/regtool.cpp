// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Small Windows-native registry tool.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr REGTOOL_OPTIONS =
    // [EXT]
    std::array{OPTION("", "", "operate on the Windows registry", STRING_TYPE)};

namespace regtool_pipeline {

struct RegPath {
  HKEY root = nullptr;
  std::wstring subkey;
};

class UniqueRegKey {
 public:
  UniqueRegKey() = default;
  explicit UniqueRegKey(HKEY key) : key_(key) {}

  UniqueRegKey(const UniqueRegKey&) = delete;
  auto operator=(const UniqueRegKey&) -> UniqueRegKey& = delete;

  UniqueRegKey(UniqueRegKey&& other) noexcept : key_(other.release()) {}

  auto operator=(UniqueRegKey&& other) noexcept -> UniqueRegKey& {
    if (this != &other) reset(other.release());
    return *this;
  }

  ~UniqueRegKey() { reset(); }

  [[nodiscard]] auto get() const noexcept -> HKEY { return key_; }
  explicit operator bool() const noexcept { return key_ != nullptr; }

  auto release() noexcept -> HKEY {
    HKEY key = key_;
    key_ = nullptr;
    return key;
  }

  auto reset(HKEY key = nullptr) noexcept -> void {
    if (key_ != nullptr) RegCloseKey(key_);
    key_ = key;
  }

 private:
  HKEY key_ = nullptr;
};

auto fail(std::string_view message) -> int {
  safeErrorPrintLn("regtool: " + std::string(message));
  return 1;
}

auto fail_win32(std::string_view action, LSTATUS status) -> int {
  safeErrorPrintLn("regtool: " + std::string(action) + ": " +
                   win32_posix_error_text(static_cast<unsigned long>(status)));
  return 1;
}

auto normalize_registry_path(std::string_view path) -> std::string {
  std::string text(path);
  for (auto& ch : text) {
    if (ch == '\\') ch = '/';
  }
  while (!text.empty() && text.front() == '/') {
    text.erase(text.begin());
  }
  return text;
}

auto parse_path(std::string_view raw_path)
    -> std::expected<RegPath, std::string> {
  std::string path = normalize_registry_path(raw_path);
  auto slash = path.find('/');
  std::string root_name =
      slash == std::string::npos ? path : path.substr(0, slash);
  std::string subkey = slash == std::string::npos ? "" : path.substr(slash + 1);

  HKEY root = nullptr;
  if (ascii_iequals(root_name, "HKCU") ||
      ascii_iequals(root_name, "HKEY_CURRENT_USER")) {
    root = HKEY_CURRENT_USER;
  } else if (ascii_iequals(root_name, "HKLM") ||
             ascii_iequals(root_name, "HKEY_LOCAL_MACHINE")) {
    root = HKEY_LOCAL_MACHINE;
  } else if (ascii_iequals(root_name, "HKCR") ||
             ascii_iequals(root_name, "HKEY_CLASSES_ROOT")) {
    root = HKEY_CLASSES_ROOT;
  } else if (ascii_iequals(root_name, "HKU") ||
             ascii_iequals(root_name, "HKEY_USERS")) {
    root = HKEY_USERS;
  } else if (ascii_iequals(root_name, "HKCC") ||
             ascii_iequals(root_name, "HKEY_CURRENT_CONFIG")) {
    root = HKEY_CURRENT_CONFIG;
  } else {
    return std::unexpected("unknown registry root '" + root_name + "'");
  }

  for (auto& ch : subkey) {
    if (ch == '/') ch = '\\';
  }
  return RegPath{.root = root, .subkey = utf8_to_wstring(subkey)};
}

auto value_name(std::string_view name) -> std::wstring {
  if (name == "@" || name == ".") return {};
  return utf8_to_wstring(std::string(name));
}

auto open_key(const RegPath& path, REGSAM access)
    -> std::expected<UniqueRegKey, LSTATUS> {
  HKEY key = nullptr;
  LSTATUS status =
      RegOpenKeyExW(path.root, path.subkey.c_str(), 0, access, &key);
  if (status != ERROR_SUCCESS) return std::unexpected(status);
  return UniqueRegKey(key);
}

auto create_key(const RegPath& path, REGSAM access)
    -> std::expected<UniqueRegKey, LSTATUS> {
  HKEY key = nullptr;
  DWORD disposition = 0;
  LSTATUS status = RegCreateKeyExW(path.root, path.subkey.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, access, nullptr,
                                   &key, &disposition);
  if (status != ERROR_SUCCESS) return std::unexpected(status);
  return UniqueRegKey(key);
}

auto print_registry_value(HKEY key, const std::wstring& name) -> int {
  DWORD type = 0;
  DWORD bytes = 0;
  const wchar_t* value = name.empty() ? nullptr : name.c_str();
  LSTATUS status =
      RegQueryValueExW(key, value, nullptr, &type, nullptr, &bytes);
  if (status != ERROR_SUCCESS) return fail_win32("query value", status);

  std::vector<std::byte> data(bytes == 0 ? 1 : bytes);
  status = RegQueryValueExW(key, value, nullptr, &type,
                            reinterpret_cast<LPBYTE>(data.data()), &bytes);
  if (status != ERROR_SUCCESS) return fail_win32("query value", status);

  switch (type) {
    case REG_SZ:
    case REG_EXPAND_SZ: {
      std::wstring text(reinterpret_cast<const wchar_t*>(data.data()),
                        bytes / sizeof(wchar_t));
      while (!text.empty() && text.back() == L'\0') text.pop_back();
      safePrintLn(wstring_to_utf8(text));
      return 0;
    }
    case REG_DWORD: {
      if (bytes < sizeof(DWORD)) return fail("malformed REG_DWORD value");
      DWORD number = 0;
      std::memcpy(&number, data.data(), sizeof(number));
      safePrintLn(std::to_string(number));
      return 0;
    }
    case REG_QWORD: {
      if (bytes < sizeof(unsigned long long)) {
        return fail("malformed REG_QWORD value");
      }
      unsigned long long number = 0;
      std::memcpy(&number, data.data(), sizeof(number));
      safePrintLn(std::to_string(number));
      return 0;
    }
    default: {
      std::ostringstream out;
      out << std::hex << std::setfill('0');
      for (DWORD i = 0; i < bytes; ++i) {
        out << std::setw(2)
            << static_cast<unsigned int>(
                   reinterpret_cast<unsigned char*>(data.data())[i]);
      }
      safePrintLn(out.str());
      return 0;
    }
  }
}

auto list_key(HKEY key) -> int {
  DWORD subkeys = 0;
  DWORD max_subkey = 0;
  DWORD values = 0;
  DWORD max_value = 0;
  LSTATUS status =
      RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, &max_subkey,
                       nullptr, &values, &max_value, nullptr, nullptr, nullptr);
  if (status != ERROR_SUCCESS) return fail_win32("query key", status);

  std::wstring name(std::max<DWORD>(max_subkey, 1) + 1, L'\0');
  for (DWORD i = 0; i < subkeys; ++i) {
    DWORD len = static_cast<DWORD>(name.size());
    status = RegEnumKeyExW(key, i, name.data(), &len, nullptr, nullptr, nullptr,
                           nullptr);
    if (status != ERROR_SUCCESS) return fail_win32("enumerate subkeys", status);
    safePrintLn(wstring_to_utf8(std::wstring_view(name.data(), len)) + "/");
  }

  name.assign(std::max<DWORD>(max_value, 1) + 1, L'\0');
  for (DWORD i = 0; i < values; ++i) {
    DWORD len = static_cast<DWORD>(name.size());
    status = RegEnumValueW(key, i, name.data(), &len, nullptr, nullptr, nullptr,
                           nullptr);
    if (status != ERROR_SUCCESS) return fail_win32("enumerate values", status);
    safePrintLn(
        len == 0 ? "@" : wstring_to_utf8(std::wstring_view(name.data(), len)));
  }
  return 0;
}

auto parse_reg_path_arg(std::string_view raw_path)
    -> std::expected<RegPath, std::string> {
  auto path = parse_path(raw_path);
  if (!path) return std::unexpected(path.error());
  if (path->subkey.empty())
    return std::unexpected("root-only paths are not supported");
  return path;
}

auto run_list(std::span<const std::string_view> args) -> int {
  if (args.size() != 2) return fail("usage: regtool list KEY");
  auto path = parse_reg_path_arg(args[1]);
  if (!path) return fail(path.error());
  auto key = open_key(*path, KEY_READ);
  if (!key) return fail_win32("open key", key.error());
  return list_key(key->get());
}

auto run_get(std::span<const std::string_view> args) -> int {
  if (args.size() != 2 && args.size() != 3) {
    return fail("usage: regtool get KEY [VALUE]");
  }
  auto path = parse_reg_path_arg(args[1]);
  if (!path) return fail(path.error());
  auto key = open_key(*path, KEY_QUERY_VALUE);
  if (!key) return fail_win32("open key", key.error());
  std::wstring name = args.size() == 3 ? value_name(args[2]) : std::wstring{};
  return print_registry_value(key->get(), name);
}

auto run_set(std::span<const std::string_view> args) -> int {
  if (args.size() != 4) return fail("usage: regtool set KEY VALUE DATA");
  auto path = parse_reg_path_arg(args[1]);
  if (!path) return fail(path.error());
  auto key = create_key(*path, KEY_SET_VALUE);
  if (!key) return fail_win32("create key", key.error());

  std::wstring name = value_name(args[2]);
  std::wstring data = utf8_to_wstring(std::string(args[3]));
  LSTATUS status =
      RegSetValueExW(key->get(), name.empty() ? nullptr : name.c_str(), 0,
                     REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
                     static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
  if (status != ERROR_SUCCESS) return fail_win32("set value", status);
  return 0;
}

auto run_remove(std::span<const std::string_view> args) -> int {
  if (args.size() != 2 && args.size() != 3) {
    return fail("usage: regtool remove KEY [VALUE]");
  }
  auto path = parse_reg_path_arg(args[1]);
  if (!path) return fail(path.error());

  if (args.size() == 3) {
    auto key = open_key(*path, KEY_SET_VALUE);
    if (!key) return fail_win32("open key", key.error());
    std::wstring name = value_name(args[2]);
    LSTATUS status =
        RegDeleteValueW(key->get(), name.empty() ? nullptr : name.c_str());
    if (status != ERROR_SUCCESS) return fail_win32("delete value", status);
    return 0;
  }

  LSTATUS status = RegDeleteTreeW(path->root, path->subkey.c_str());
  if (status != ERROR_SUCCESS) return fail_win32("delete key", status);
  return 0;
}

auto run(const CommandContext<REGTOOL_OPTIONS.size()>& ctx) -> int {
  std::vector<std::string_view> args;
  args.reserve(ctx.positionals.size());
  for (auto arg : ctx.positionals) args.push_back(arg);

  if (args.empty()) {
    return fail("usage: regtool ACTION KEY [VALUE] [DATA]");
  }

  if (ascii_iequals(args[0], "list")) return run_list(args);
  if (ascii_iequals(args[0], "get")) return run_get(args);
  if (ascii_iequals(args[0], "set")) return run_set(args);
  if (ascii_iequals(args[0], "remove") || ascii_iequals(args[0], "rm") ||
      ascii_iequals(args[0], "delete")) {
    return run_remove(args);
  }

  return fail("unknown action '" + std::string(args[0]) + "'");
}

}  // namespace regtool_pipeline

REGISTER_COMMAND(regtool, "regtool", "regtool ACTION KEY [VALUE] [DATA]",
                 "Small Windows-native registry tool.",
                 "  regtool list HKCU/Software\n"
                 "  regtool get HKCU/Software/App Name\n"
                 "  regtool set HKCU/Software/App Name Value\n"
                 "  regtool remove HKCU/Software/App Name",
                 "reg.exe(1), hostid(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", REGTOOL_OPTIONS) {
  return regtool_pipeline::run(ctx);
}
