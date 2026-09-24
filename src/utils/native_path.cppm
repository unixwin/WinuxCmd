// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

export auto attributes_w(std::wstring_view path) -> DWORD;
export auto attributes_are_directory(DWORD attrs) -> bool;
export auto attributes_are_regular_file(DWORD attrs) -> bool;
export auto valid_attributes(DWORD attrs) -> bool;

export auto normalize_api_operand_w(std::wstring_view path) -> std::wstring {
  const bool had_trailing_separator =
      strip_trailing_separators(path).size() != path.size();
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
    normalized = normalize_separators(std::move(drive_path));
  }
  // A trailing separator means the operand must name a directory (POSIX
  // ENOTDIR). If the stripped target is not a directory, keep a separator so
  // the OS rejects opens/creates instead of silently touching the regular
  // file (#1052 — stripping turned "file/" into "file" and truncated it).
  if (had_trailing_separator &&
      !attributes_are_directory(attributes_w(normalized))) {
    normalized.push_back(L'\\');
  }
  return normalized;
}

export auto resolve_pseudo_device_w(std::wstring_view path)
    -> std::optional<std::wstring>;

// [GNU] Standard-stream pseudo paths: /dev/std{in,out,err} plus the
// /dev/fd/N and /proc/*/fd/N spellings. MSYS/Git-Bash expands /dev/std* in
// native argv to /proc/self/fd/N (sometimes already resolved to
// /proc/<pid>/fd/N), so every spelling is recognized here (#1056).
// Returns the CRT file descriptor (0, 1, 2) the operand refers to.
export auto pseudo_device_std_fd_w(std::wstring_view path)
    -> std::optional<int>;

export auto pseudo_device_std_fd(std::string_view path) -> std::optional<int> {
  return pseudo_device_std_fd_w(from_utf8(path));
}

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
export auto pseudo_device_std_fd_w(std::wstring_view path)
    -> std::optional<int> {
  if (path == L"/dev/stdin") return 0;
  if (path == L"/dev/stdout") return 1;
  if (path == L"/dev/stderr") return 2;

  const auto fd_of_tail = [](std::wstring_view tail) -> std::optional<int> {
    if (tail.size() == 1 && tail[0] >= L'0' && tail[0] <= L'2') {
      return static_cast<int>(tail[0] - L'0');
    }
    return std::nullopt;
  };

  if (path.starts_with(L"/dev/fd/")) {
    return fd_of_tail(path.substr(8));
  }
  // /proc/self/fd/N and the MSYS-resolved /proc/<pid>/fd/N form.
  if (path.starts_with(L"/proc/")) {
    const std::wstring_view rest = path.substr(6);
    const auto fd_pos = rest.find(L"/fd/");
    if (fd_pos != std::wstring_view::npos) {
      const std::wstring_view pid = rest.substr(0, fd_pos);
      const bool valid_pid =
          !pid.empty() &&
          (pid == L"self" || std::ranges::all_of(pid, [](wchar_t ch) {
             return ch >= L'0' && ch <= L'9';
           }));
      if (valid_pid) return fd_of_tail(rest.substr(fd_pos + 4));
    }
  }
  return std::nullopt;
}

export auto resolve_pseudo_device_w(std::wstring_view path)
    -> std::optional<std::wstring> {
  if (path == L"/dev/null") return std::wstring(L"NUL");
  if (path == L"/dev/tty") return std::wstring(L"CONIN$");
  if (auto fd = pseudo_device_std_fd_w(path)) {
    return *fd == 0 ? std::wstring(L"CONIN$") : std::wstring(L"CONOUT$");
  }
  // Bare DOS device names: MSYS/Git-Bash rewrites /dev/null to "nul" in
  // native argv, and users may type NUL/CON directly. They resolve only in
  // the Win32 device namespace, so they must pass through without the \\?\
  // prefix (#1055).
  static constexpr std::wstring_view kDosDevices[] = {
      L"NUL",  L"CON",  L"PRN",  L"AUX",  L"CONIN$", L"CONOUT$",
      L"COM1", L"COM2", L"COM3", L"COM4", L"COM5",   L"COM6",
      L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2",   L"LPT3",
      L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8",   L"LPT9"};
  for (const auto device : kDosDevices) {
    if (path.size() != device.size()) continue;
    bool equal = true;
    for (size_t i = 0; i < path.size(); ++i) {
      wchar_t a = path[i];
      wchar_t b = device[i];
      if (a >= L'a' && a <= L'z') a = static_cast<wchar_t>(a - L'a' + L'A');
      if (b >= L'a' && b <= L'z') b = static_cast<wchar_t>(b - L'a' + L'A');
      if (a != b) {
        equal = false;
        break;
      }
    }
    if (equal) return std::wstring(path);
  }
  return std::nullopt;
}

// True when the operand is a bare DOS device name.
//
// Device names resolve through the Win32 device namespace, which the \\?\
// extended-length prefix bypasses: to_extended_path would run
// GetFullPathNameW("NUL"), resolve the name against the current directory and
// hand back "\\?\<cwd>\NUL" - an ordinary, missing file. Every Win32 call must
// therefore receive these names verbatim, which is exactly the invariant
// make_api_path_operand_w establishes when it resolves a pseudo-device; this
// predicate lets the attribute probes honour it too. Verified with
// GetFileAttributesW: "NUL" -> 0x20 (valid), "\\?\<cwd>\NUL" -> INVALID.
// Keep the list in sync with resolve_pseudo_device_w, which is where the
// pseudo-devices that reach this function are produced.
export auto is_dos_device_name_w(std::wstring_view path) -> bool {
  path = strip_trailing_separators(path);

  // An already explicit device namespace ("\\.\NUL") needs no adjustment.
  if (path.size() > 4 && path.compare(0, 4, L"\\\\.\\") == 0) return true;

  // A device name only resolves when it is the whole operand: "dir\NUL" names
  // an ordinary file, and so does "\\?\C:\dir\NUL".
  if (path.find_first_of(L"\\/") != std::wstring_view::npos) return false;

  std::wstring upper;
  upper.reserve(path.size());
  for (const wchar_t ch : path) {
    upper.push_back((ch >= L'a' && ch <= L'z')
                        ? static_cast<wchar_t>(ch - L'a' + L'A')
                        : ch);
  }

  if (upper == L"NUL" || upper == L"CON" || upper == L"PRN" ||
      upper == L"AUX" || upper == L"CONIN$" || upper == L"CONOUT$") {
    return true;
  }
  return upper.size() == 4 &&
         (upper.starts_with(L"COM") || upper.starts_with(L"LPT")) &&
         upper[3] >= L'1' && upper[3] <= L'9';
}

// True when the operand denotes a Windows character device, either directly as
// a DOS device name or as a POSIX pseudo-device that resolves onto one. GNU
// reports /dev/null as a character special file, so `test -c /dev/null` is true
// and `test -b /dev/null` is false there; both predicates need this answer.
export auto is_character_device_w(std::wstring_view path) -> bool {
  if (auto pseudo = resolve_pseudo_device_w(path)) {
    return is_dos_device_name_w(*pseudo);
  }
  return is_dos_device_name_w(path);
}

export auto is_character_device(std::string_view path) -> bool {
  return is_character_device_w(from_utf8(path));
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
      strip_trailing_separators(std::wstring_view(operand.original)).size() !=
      operand.original.size();
  return operand;
}

export auto make_api_path_operand(std::string_view path) -> ApiPathOperand {
  return make_api_path_operand_w(from_utf8(path));
}

// Probe the target's attributes ignoring the trailing-separator enforcement:
// extended keeps the separator for non-directory operands (#1052), which
// GetFileAttributesW rejects, so strip it before probing.
export auto operand_target_attributes_w(const ApiPathOperand& operand)
    -> DWORD {
  const std::wstring_view probe =
      operand.had_trailing_separator
          ? strip_trailing_separators(std::wstring_view(operand.extended))
          : std::wstring_view(operand.extended);
  return attributes_w(probe);
}

// errno-style error for opening/creating a file at this operand: 0 when the
// operand may proceed; ENOTDIR when a trailing separator named a regular
// file on a read-open; ENOENT when it named a missing path (#1052).
// For write/create opens GNU mirrors the Linux quirk where O_CREAT on
// "file/" reports EISDIR, and opening a directory for writing is EISDIR too.
export auto operand_file_open_error(const ApiPathOperand& operand,
                                    bool for_write = false) -> int {
  const DWORD attrs = operand_target_attributes_w(operand);
  if (operand.had_trailing_separator) {
    if (!valid_attributes(attrs)) return ENOENT;
    if (attributes_are_directory(attrs)) return for_write ? EISDIR : 0;
    return for_write ? EISDIR : ENOTDIR;
  }
  if (for_write && attributes_are_directory(attrs)) return EISDIR;
  return 0;
}

export auto attributes_w(std::wstring_view path) -> DWORD {
  // DOS device names resolve only in the Win32 device namespace, which the
  // \\?\ extended-length prefix bypasses (see is_dos_device_name_w): probe
  // them verbatim. resolve_pseudo_device_w also covers the /dev/* spellings
  // and the MSYS-rewritten bare names such as "nul".
  std::wstring_view probe = path;
  std::wstring resolved;
  if (auto pseudo = resolve_pseudo_device_w(path)) {
    resolved = std::move(*pseudo);
    probe = resolved;
  }
  if (is_dos_device_name_w(probe)) {
    const std::wstring verbatim(strip_trailing_separators(probe));
    return GetFileAttributesW(verbatim.c_str());
  }

  // Keep all attribute probes on the same extended-path API boundary as
  // file_io. This avoids MAX_PATH failures for otherwise valid paths.
  const std::wstring native = probe.starts_with(L"\\\\?\\")
                                  ? std::wstring(probe)
                                  : to_extended_path(probe);
  return GetFileAttributesW(native.c_str());
}

// Full attribute record for an operand that may name a character device.
//
// GetFileAttributesExW is the one attribute API that rejects DOS device names
// outright. Probed against kernel32 on Windows 11:
//
//   GetFileAttributesW("NUL")      -> 0x20   (FILE_ATTRIBUTE_ARCHIVE)
//   GetFileAttributesExW("NUL")    -> FALSE, ERROR_INVALID_PARAMETER (87)
//   GetFileAttributesExW("CONIN$") -> FALSE, ERROR_INVALID_FUNCTION (1)
//
// So a tool that renders a full stat record cannot use the Ex variant for
// /dev/* operands: the device resolves correctly and then the query fails. This
// helper keeps both APIs behind one device-aware call - device names are
// answered from GetFileAttributesW with a zeroed size/time record, which is
// exactly what a character device reports anyway (GNU prints a size of 0 for
// /dev/null). Ordinary paths keep the Ex variant so long paths and reparse
// points behave as before.
export auto file_attribute_data_w(std::wstring_view path,
                                  WIN32_FILE_ATTRIBUTE_DATA& out) -> bool {
  out = WIN32_FILE_ATTRIBUTE_DATA{};

  std::wstring probe;
  if (auto pseudo = resolve_pseudo_device_w(path)) {
    probe = *pseudo;
  } else {
    probe.assign(path);
  }

  if (is_dos_device_name_w(probe)) {
    const std::wstring verbatim(strip_trailing_separators(probe));
    const DWORD attrs = GetFileAttributesW(verbatim.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    out.dwFileAttributes = attrs;
    return true;
  }

  const std::wstring native =
      probe.starts_with(L"\\\\?\\") ? probe : to_extended_path(probe);
  return GetFileAttributesExW(native.c_str(), GetFileExInfoStandard, &out) != 0;
}

export auto file_attribute_data(std::string_view path,
                                WIN32_FILE_ATTRIBUTE_DATA& out) -> bool {
  return file_attribute_data_w(from_utf8(path), out);
}

export auto valid_attributes(DWORD attrs) -> bool {
  return attrs != INVALID_FILE_ATTRIBUTES;
}

// [GNU] canonicalize_filename_mode() existence rules shared by realpath and
// readlink (-f/-e/-m).
export enum class CanonMode {
  all_but_last,  // every component but the last must exist (realpath, -f)
  existing,      // every component must exist (realpath -e, readlink -e)
  missing,       // no component needs to exist (realpath -m, readlink -m)
};

export auto canonicalize_path_w(std::wstring_view path, CanonMode mode,
                                bool expand_symlinks, bool logical)
    -> std::expected<std::wstring, int>;

// On-disk representation of a WinuxCmd FIFO (#1038). Windows has no
// filesystem FIFO node type, and MSYS2/Cygwin keep their FIFOs purely
// in-runtime (invisible to native tools), so we emulate: a regular file
// whose first bytes are kFifoMarker carrying FILE_ATTRIBUTE_SYSTEM, the
// same trick Cygwin historically used for non-native symlinks.
// Reader/writer opens in file_io bridge the marker to a deterministic
// \\.\pipe\winuxcmd-fifo-<hash> endpoint, so blocking pipe semantics work
// between WinuxCmd processes; non-WinuxCmd readers simply see the marker.
export constexpr std::string_view kFifoMarker = "!<fifo>";

export auto is_winux_fifo_w(std::wstring_view path) -> bool {
  const DWORD attrs = attributes_w(path);
  if (!valid_attributes(attrs) || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      (attrs & FILE_ATTRIBUTE_SYSTEM) == 0) {
    return false;
  }
  const std::wstring native =
      to_extended_path(std::wstring(strip_trailing_separators(path)));
  HANDLE file =
      CreateFileW(native.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  std::array<char, kFifoMarker.size()> buffer{};
  DWORD read = 0;
  const bool ok = ReadFile(file, buffer.data(),
                           static_cast<DWORD>(buffer.size()), &read, nullptr);
  CloseHandle(file);
  return ok && read == buffer.size() &&
         std::string_view(buffer.data(), read) == kFifoMarker;
}

export auto is_winux_fifo(std::string_view path) -> bool {
  return is_winux_fifo_w(from_utf8(path));
}

// Deterministic \\.\pipe endpoint for a marker file, keyed on the
// canonical path so independent processes rendezvous on the same name.
export auto winux_fifo_pipe_name_w(std::wstring_view path) -> std::wstring {
  std::wstring canon;
  if (auto resolved =
          canonicalize_path_w(path, CanonMode::missing, true, false);
      resolved) {
    canon = std::move(*resolved);
  } else {
    canon.assign(path);
  }
  for (auto& ch : canon) {
    if (ch == L'/') ch = L'\\';
    if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
  }
  uint64_t hash = 14695981039346656037ull;  // FNV-1a
  for (const wchar_t ch : canon) {
    for (const int byte : {ch & 0xFF, (ch >> 8) & 0xFF}) {
      hash = (hash ^ static_cast<uint8_t>(byte)) * 1099511628211ull;
    }
  }
  std::wstring name = L"\\\\.\\pipe\\winuxcmd-fifo-";
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx",
                static_cast<unsigned long long>(hash));
  name.append(hex, hex + 16);
  return name;
}

// Create a fifo marker file. Returns ERROR_SUCCESS or the Win32 error
// (ERROR_FILE_EXISTS when the name is already taken).
export auto create_winux_fifo_w(std::wstring_view path, bool readonly = false)
    -> DWORD {
  const std::wstring native =
      to_extended_path(std::wstring(strip_trailing_separators(path)));
  HANDLE file = CreateFileW(native.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return GetLastError();
  DWORD written = 0;
  const bool ok =
      WriteFile(file, kFifoMarker.data(),
                static_cast<DWORD>(kFifoMarker.size()), &written, nullptr) &&
      written == kFifoMarker.size();
  const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(native.c_str());
    return write_error;
  }
  DWORD attrs = FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE;
  if (readonly) attrs |= FILE_ATTRIBUTE_READONLY;
  SetFileAttributesW(native.c_str(), attrs);
  return ERROR_SUCCESS;
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
  // Use the extended-path form so `mkdir -p` and other recursive creators
  // work beyond MAX_PATH like the rest of the API boundary (#1061).
  const std::wstring native = to_extended_path(path);
  if (std::filesystem::create_directories(std::filesystem::path(native), ec)) {
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

namespace canonicalize_detail {
// Split the relative part of a path into components, keeping "." and ".."
// entries so they can be applied against the resolved stack.
inline auto components_of(const std::filesystem::path& path)
    -> std::deque<std::filesystem::path> {
  std::deque<std::filesystem::path> parts;
  for (const auto& part : path.relative_path()) {
    parts.push_back(part);
  }
  return parts;
}

// Read the substitution text of a symlink component.  Returns nullopt for
// non-symlink reparse points (mount points carry a \\??\Volume{GUID} target
// that cannot be re-expressed as a Win32 path; leaving the component in
// place still lets the kernel resolve it).
inline auto symlink_target(const std::filesystem::path& path)
    -> std::optional<std::filesystem::path> {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  if (ec || !std::filesystem::is_symlink(status)) {
    return std::nullopt;
  }
  auto target = std::filesystem::read_symlink(path, ec);
  if (ec) {
    return std::nullopt;
  }
  std::wstring text = target.wstring();
  if (text.rfind(L"\\??\\", 0) == 0 || text.rfind(L"\\\\?\\", 0) == 0) {
    // UNC device form: "\\??\UNC\server\share" -> "\\server\share".
    if (text.size() > 8 && (text.compare(4, 4, L"UNC\\") == 0)) {
      text = L"\\\\" + text.substr(8);
    } else {
      text = text.substr(4);
    }
  }
  if (text.rfind(L"Volume{", 0) == 0) {
    return std::nullopt;  // mount point: keep the component verbatim
  }
  return std::filesystem::path(text);
}
}  // namespace canonicalize_detail

// [GNU] Resolve PATH component-wise, substituting symlink targets as each
// component is reached (realpath/readlink -f/-e/-m semantics).  Returns the
// resolved absolute path, or an errno value (ENOENT when a required
// component does not exist, ELOOP on a symlink cycle, ENOTDIR when a
// trailing separator names a non-directory).  When the resolved path fully
// exists it is re-expressed with on-disk casing via the file handle.
export auto canonicalize_path_w(std::wstring_view path, CanonMode mode,
                                bool expand_symlinks = true,
                                bool logical = false)
    -> std::expected<std::wstring, int> {
  std::filesystem::path input{std::wstring(path)};
  const bool needs_directory =
      strip_trailing_separators(path).size() != path.size();

  std::filesystem::path current;
  std::deque<std::filesystem::path> pending;
  if (input.is_absolute()) {
    current = input.root_path();
    pending = canonicalize_detail::components_of(input);
  } else {
    current = std::filesystem::path(current_directory_w());
    pending = canonicalize_detail::components_of(input);
  }
  if (current.empty()) {
    return std::unexpected(ENOENT);
  }

  // -L/--logical folds "." and ".." lexically before symlink substitution.
  if (logical) {
    std::filesystem::path folded = (current / input.relative_path());
    folded = folded.lexically_normal();
    current = folded.root_path();
    pending = canonicalize_detail::components_of(folded);
  }

  int symlink_budget = 40;  // POSIX MAXSYMLINKS
  while (!pending.empty()) {
    const std::filesystem::path part = pending.front();
    pending.pop_front();

    const auto& native = part.native();
    if (native.empty() || native == L".") {
      continue;
    }
    if (native == L"..") {
      // Physical mode pops the *resolved* stack so "linkdir/../x" ascends
      // inside the link target's parent.
      if (current != current.root_path()) {
        current = current.parent_path();
      }
      continue;
    }

    std::filesystem::path candidate = current / part;
    const DWORD attrs = attributes_w(candidate.wstring());
    if (!valid_attributes(attrs)) {
      if (mode == CanonMode::missing) {
        current = candidate;  // keep appending; ".."-handling still works
        continue;
      }
      // [GNU] Traversing through a component that exists but is not a
      // directory reports ENOTDIR (readlink("file/x") fails ENOTDIR, not
      // ENOENT), in every mode that probes components.
      const DWORD parent_attrs = attributes_w(current.wstring());
      if (valid_attributes(parent_attrs) &&
          !attributes_are_directory(parent_attrs)) {
        return std::unexpected(ENOTDIR);
      }
      // [GNU] A component longer than NAME_MAX fails the per-component
      // lookup with ENAMETOOLONG; the CAN_ALL_BUT_LAST exemption only
      // covers ENOENT, and CAN_MISSING never probes (#370).
      constexpr size_t kMaxComponentLength = 255;  // NTFS NAME_MAX
      if (part.native().size() > kMaxComponentLength) {
        return std::unexpected(ENAMETOOLONG);
      }
      // [GNU] The CAN_ALL_BUT_LAST exemption applies when nothing but
      // separators follows the component (a trailing separator still
      // counts as the last component).  std::filesystem iteration yields
      // an empty element for a trailing separator.
      const bool only_separators_remain =
          std::ranges::all_of(pending, [](const std::filesystem::path& rem) {
            return rem.native().empty();
          });
      if (mode == CanonMode::all_but_last && only_separators_remain) {
        current = candidate;  // the last component may be missing
        continue;
      }
      return std::unexpected(ENOENT);
    }

    if (expand_symlinks) {
      if (auto target = canonicalize_detail::symlink_target(candidate)) {
        if (--symlink_budget <= 0) {
          return std::unexpected(ELOOP);
        }
        auto target_parts = canonicalize_detail::components_of(*target);
        if (target->is_absolute()) {
          current = target->root_path();
        }
        pending.insert(pending.begin(), target_parts.begin(),
                       target_parts.end());
        continue;
      }
    }
    current = candidate;
  }

  if (needs_directory) {
    // [GNU] A trailing separator makes the leaf require a directory, but a
    // *missing* leaf is still exempt under CAN_ALL_BUT_LAST (the ENOENT
    // exemption ignores trailing slashes).  Only an existing non-directory
    // reports ENOTDIR.
    const DWORD final_attrs = attributes_w(current.wstring());
    if (valid_attributes(final_attrs) &&
        !attributes_are_directory(final_attrs)) {
      return std::unexpected(ENOTDIR);
    }
  }

  // Prefer the kernel-canonical spelling (resolves 8.3 names and restores
  // on-disk casing) whenever the whole path exists.
  const std::wstring finished = current.wstring();
  if (mode != CanonMode::missing && valid_attributes(attributes_w(finished))) {
    const std::wstring native = to_extended_path(finished);
    HANDLE handle = CreateFileW(
        native.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
      std::wstring buffer(32768, L'\0');
      DWORD written = GetFinalPathNameByHandleW(
          handle, buffer.data(), static_cast<DWORD>(buffer.size()),
          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
      CloseHandle(handle);
      if (written > 0 && written < buffer.size()) {
        buffer.resize(written);
        if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) {
          return L"\\\\" + buffer.substr(8);
        }
        if (buffer.rfind(L"\\\\?\\", 0) == 0) {
          return buffer.substr(4);
        }
        return buffer;
      }
    }
  }
  return finished;
}

export auto canonicalize_path(std::string_view path, CanonMode mode,
                              bool expand_symlinks = true, bool logical = false)
    -> std::expected<std::string, int> {
  auto resolved =
      canonicalize_path_w(from_utf8(path), mode, expand_symlinks, logical);
  if (!resolved) {
    return std::unexpected(resolved.error());
  }
  return to_utf8(*resolved);
}

}  // namespace native_path
