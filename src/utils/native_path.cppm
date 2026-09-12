/*
 *  Copyright © 2026 [caomengxuan666]
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
 */
module;

#include "pch/pch.h"

export module utils:native_path;

import std;
import :utf8;

namespace native_path {

export auto from_utf8(std::string_view path) -> std::wstring {
  return utf8_to_wstring(path);
}

export auto to_utf8(std::wstring_view path) -> std::string {
  return wstring_to_utf8(path);
}

export auto normalize_separators(std::wstring path) -> std::wstring {
  for (auto& ch : path) {
    if (ch == L'/') ch = L'\\';
  }
  return path;
}

export auto normalize_separators_utf8(std::string path) -> std::string {
  for (auto& ch : path) {
    if (ch == '/') ch = '\\';
  }
  return path;
}

export auto is_separator(wchar_t ch) -> bool {
  return ch == L'\\' || ch == L'/';
}

export auto is_separator(char ch) -> bool { return ch == '\\' || ch == '/'; }

export auto unc_root_length(std::wstring_view path) -> std::optional<size_t> {
  if (path.size() < 3 || !is_separator(path[0]) || !is_separator(path[1])) {
    return std::nullopt;
  }

  const auto server_end = path.find_first_of(L"\\/", 2);
  if (server_end == std::wstring_view::npos) return path.size();

  const auto share_start = server_end + 1;
  const auto share_end = path.find_first_of(L"\\/", share_start);
  if (share_end == std::wstring_view::npos) return path.size();
  return share_end;
}

export auto unc_root_length(std::string_view path) -> std::optional<size_t> {
  if (path.size() < 3 || !is_separator(path[0]) || !is_separator(path[1])) {
    return std::nullopt;
  }

  const auto server_end = path.find_first_of("\\/", 2);
  if (server_end == std::string_view::npos) return path.size();

  const auto share_start = server_end + 1;
  const auto share_end = path.find_first_of("\\/", share_start);
  if (share_end == std::string_view::npos) return path.size();
  return share_end;
}

export auto strip_trailing_separators(std::wstring_view path)
    -> std::wstring_view {
  while (path.size() > 1 && is_separator(path.back())) {
    if (path.size() == 3 && path[1] == L':') break;
    if (auto root_len = unc_root_length(path);
        root_len && path.size() <= *root_len + 1) {
      break;
    }
    path.remove_suffix(1);
  }
  return path;
}

export auto strip_trailing_separators(std::string_view path)
    -> std::string_view {
  while (path.size() > 1 && is_separator(path.back())) {
    if (path.size() == 3 && path[1] == ':') break;
    if (auto root_len = unc_root_length(path);
        root_len && path.size() <= *root_len + 1) {
      break;
    }
    path.remove_suffix(1);
  }
  return path;
}

export auto normalize_api_operand_w(std::wstring_view path) -> std::wstring {
  std::wstring normalized(strip_trailing_separators(path));
  if (normalized.size() >= 2 && is_separator(normalized[0]) &&
      ((normalized[1] >= L'a' && normalized[1] <= L'z') ||
       (normalized[1] >= L'A' && normalized[1] <= L'Z')) &&
      (normalized.size() == 2 || is_separator(normalized[2]))) {
    std::wstring drive_path;
    wchar_t drive = normalized[1];
    if (drive >= L'a' && drive <= L'z') {
      drive = static_cast<wchar_t>(drive - L'a' + L'A');
    }
    drive_path.push_back(drive);
    drive_path.append(L":\\");
    if (normalized.size() > 3) {
      drive_path.append(normalized.substr(3));
    }
    return normalize_separators(std::move(drive_path));
  }
  return normalized;
}

export auto resolve_pseudo_device_w(std::wstring_view path)
    -> std::optional<std::wstring>;

export auto normalize_api_operand(std::string_view path) -> std::string {
  // Delegate to the wide implementation so MSYS/Git-Bash style operands such
  // as "/d/repo/file" are converted to "D:\repo\file" exactly like
  // make_api_path_operand does; otherwise return the stripped path unchanged.
  // POSIX pseudo-devices resolve to their Windows equivalents first so tools
  // reading through this boundary (od, dd, ...) see /dev/null etc. (#276).
  const std::wstring wide = from_utf8(path);
  if (auto pseudo = resolve_pseudo_device_w(wide)) {
    return to_utf8(*pseudo);
  }
  return to_utf8(normalize_api_operand_w(wide));
}

export auto to_extended_path(std::wstring_view path) -> std::wstring {
  if (path.size() >= 4 && path.compare(0, 4, L"\\\\?\\") == 0) {
    return std::wstring(path);
  }

  std::wstring native(path);
  wchar_t abs_buf[32768];
  DWORD len = GetFullPathNameW(native.c_str(), 32768, abs_buf, nullptr);
  if (len == 0 || len >= 32768) return native;

  std::wstring absolute(abs_buf, len);
  if (absolute.size() >= 2 && absolute.compare(0, 2, L"\\\\") == 0) {
    return L"\\\\?\\UNC\\" + absolute.substr(2);
  }
  return L"\\\\?\\" + absolute;
}

export struct ApiPathOperand {
  std::wstring original;
  std::wstring normalized;
  std::wstring extended;
  bool had_trailing_separator = false;
};

// [GNU] POSIX pseudo-devices that GNU environments expose under /dev but
// that have no directory entry on Windows. Mapping them at the shared path
// boundary (instead of relying on an external runtime directory such as
// niubash's dev/) keeps tools like dd/tee/cat/pr working when they receive
// literal /dev/* operands (#276 follow-up, uutils#9745).
export auto resolve_pseudo_device_w(std::wstring_view path)
    -> std::optional<std::wstring> {
  if (path == L"/dev/null") return std::wstring(L"NUL");
  if (path == L"/dev/stdin") return std::wstring(L"CONIN$");
  if (path == L"/dev/stdout") return std::wstring(L"CONOUT$");
  if (path == L"/dev/stderr") return std::wstring(L"CONOUT$");
  if (path == L"/dev/tty") return std::wstring(L"CONIN$");
  return std::nullopt;
}

export auto make_api_path_operand_w(std::wstring_view path) -> ApiPathOperand {
  ApiPathOperand operand;
  operand.original = std::wstring(path);
  if (auto pseudo = resolve_pseudo_device_w(operand.original)) {
    // DOS device names (NUL, CONIN$, CONOUT$) must NOT carry the \\?\
    // prefix: the \\?\ namespace bypasses Win32 device-name resolution, and
    // GetFullPathNameW would resolve "NUL" against the current directory
    // instead. Keep the device name verbatim in both normalized and extended.
    operand.normalized = *pseudo;
    operand.extended = *pseudo;
    operand.had_trailing_separator = false;
    return operand;
  }
  operand.normalized = normalize_api_operand_w(operand.original);
  operand.extended = to_extended_path(operand.normalized);
  operand.had_trailing_separator =
      operand.normalized.size() != operand.original.size();
  return operand;
}

export auto make_api_path_operand(std::string_view path) -> ApiPathOperand {
  return make_api_path_operand_w(from_utf8(path));
}

export auto attributes_w(std::wstring_view path) -> DWORD {
  // Keep all attribute probes on the same extended-path API boundary as
  // file_io. This avoids MAX_PATH failures for otherwise valid paths.
  const std::wstring native = path.starts_with(L"\\\\?\\")
                                  ? std::wstring(path)
                                  : to_extended_path(path);
  return GetFileAttributesW(native.c_str());
}

export auto valid_attributes(DWORD attrs) -> bool {
  return attrs != INVALID_FILE_ATTRIBUTES;
}

export auto attributes_are_directory(DWORD attrs) -> bool {
  return valid_attributes(attrs) && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

export auto attributes_are_regular_file(DWORD attrs) -> bool {
  return valid_attributes(attrs) && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

export auto attributes_are_reparse_point(DWORD attrs) -> bool {
  return valid_attributes(attrs) && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

export auto current_directory_w() -> std::wstring {
  DWORD len = GetCurrentDirectoryW(0, nullptr);
  if (len == 0) return {};

  std::wstring buffer(len, L'\0');
  DWORD written = GetCurrentDirectoryW(len, buffer.data());
  if (written == 0 || written >= len) return {};
  buffer.resize(written);
  return buffer;
}

export auto current_directory() -> std::string {
  return to_utf8(current_directory_w());
}

export auto parent_path_w(std::wstring_view path) -> std::wstring {
  std::filesystem::path parsed{std::wstring(path)};
  return parsed.parent_path().wstring();
}

export auto filename_w(std::wstring_view path) -> std::wstring {
  std::filesystem::path parsed{std::wstring(path)};
  return parsed.filename().wstring();
}

export auto stem_w(std::wstring_view path) -> std::wstring {
  std::filesystem::path parsed{std::wstring(path)};
  return parsed.stem().wstring();
}

export auto extension_w(std::wstring_view path) -> std::wstring {
  std::filesystem::path parsed{std::wstring(path)};
  return parsed.extension().wstring();
}

export auto parent_path(std::string_view path) -> std::string {
  return to_utf8(parent_path_w(from_utf8(path)));
}

export auto filename(std::string_view path) -> std::string {
  return to_utf8(filename_w(from_utf8(path)));
}

export auto stem(std::string_view path) -> std::string {
  return to_utf8(stem_w(from_utf8(path)));
}

export auto extension(std::string_view path) -> std::string {
  return to_utf8(extension_w(from_utf8(path)));
}

export auto exists_w(std::wstring_view path) -> bool {
  return valid_attributes(attributes_w(path));
}

export auto is_directory_w(std::wstring_view path) -> bool {
  return attributes_are_directory(attributes_w(path));
}

export auto is_regular_file_w(std::wstring_view path) -> bool {
  return attributes_are_regular_file(attributes_w(path));
}

export auto exists(std::string_view path) -> bool {
  return exists_w(from_utf8(path));
}

export auto is_directory(std::string_view path) -> bool {
  return is_directory_w(from_utf8(path));
}

export auto is_regular_file(std::string_view path) -> bool {
  return is_regular_file_w(from_utf8(path));
}

export auto create_directory_w(std::wstring_view path) -> bool {
  std::wstring native(path);
  if (CreateDirectoryW(native.c_str(), nullptr)) return true;
  DWORD error = GetLastError();
  if (error != ERROR_ALREADY_EXISTS) return false;
  return is_directory_w(native);
}

export auto create_directories_w(std::wstring_view path) -> bool {
  std::error_code ec;
  if (std::filesystem::create_directories(std::filesystem::path(path), ec)) {
    return true;
  }
  if (!ec) return is_directory_w(path);
  return is_directory_w(path);
}

export auto create_directory(std::string_view path) -> bool {
  return create_directory_w(from_utf8(path));
}

export auto create_directories(std::string_view path) -> bool {
  return create_directories_w(from_utf8(path));
}

export auto join_w(std::wstring_view base, std::wstring_view relative)
    -> std::wstring {
  std::filesystem::path joined = std::filesystem::path(std::wstring(base)) /
                                 std::filesystem::path(std::wstring(relative));
  return joined.make_preferred().wstring();
}

export auto join(std::string_view base, std::string_view relative)
    -> std::string {
  return to_utf8(join_w(from_utf8(base), from_utf8(relative)));
}

}  // namespace native_path
