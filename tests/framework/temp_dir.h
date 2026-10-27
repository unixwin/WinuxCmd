// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#pragma once
#include <windows.h>

#include <atomic>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace temp_dir_detail {

// A component ending in '.' or ' ' that is not "." or ".." is a literal
// on-disk name (NTFS keeps such names under the \\?\ namespace), but
// GetFullPathNameW strips the suffix and absolutizes onto a different
// object — "D:\x\inner." comes back as "D:\x\inner" (#1283, the finding
// the product's native_path layer is built on). Fixtures routed through
// extended_path therefore named the stripped sibling instead: a write
// to "name." actually created "name", so the file the test meant to
// plant never existed under its literal name (the cpio trailing-dot
// pin, #1422, failed on real Windows for exactly this reason while the
// product's own operand spelling was correct).
inline bool has_literal_dot_space_component(const std::wstring &path) {
  size_t start = 0;
  for (;;) {
    const size_t end = path.find_first_of(L"\\/", start);
    const std::wstring_view component(
        path.data() + start,
        (end == std::wstring::npos ? path.size() : end) - start);
    if (!component.empty() && component != L"." && component != L".." &&
        (component.back() == L'.' || component.back() == L' ')) {
      return true;
    }
    if (end == std::wstring::npos) return false;
    start = end + 1;
  }
}

inline std::wstring current_directory() {
  wchar_t buffer[MAX_PATH * 4];
  DWORD len = GetCurrentDirectoryW(static_cast<DWORD>(std::size(buffer)),
                                   buffer);
  if (len == 0 || len >= std::size(buffer)) return {};
  return {buffer, len};
}

// Split an absolute path into its root ("D:\" or "\\server\share\") and
// the components below it.
inline std::wstring split_root(const std::wstring &abs,
                               std::vector<std::wstring_view> &components) {
  std::wstring root;
  std::wstring_view rest;
  if (abs.size() >= 2 && abs[0] == L'\\' && abs[1] == L'\\') {
    const size_t server_end = abs.find(L'\\', 2);
    const size_t share_end =
        server_end == std::wstring::npos
            ? std::wstring::npos
            : abs.find(L'\\', server_end + 1);
    const size_t root_len =
        share_end == std::wstring::npos ? abs.size() : share_end;
    root = abs.substr(0, root_len);
    rest = root_len < abs.size() ? std::wstring_view(abs).substr(root_len + 1)
                                : std::wstring_view{};
  } else {
    root = abs.substr(0, 2);
    rest = abs.size() > 3 ? std::wstring_view(abs).substr(3)
                          : std::wstring_view{};
  }
  root.push_back(L'\\');
  size_t start = 0;
  for (;;) {
    const size_t end = rest.find(L'\\', start);
    const std::wstring_view component = rest.substr(
        start, end == std::wstring_view::npos ? std::wstring_view::npos
                                              : end - start);
    if (!component.empty()) components.push_back(component);
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  return root;
}

// Absolutize lexically so components ending in '.' or ' ' survive:
// every component except "." and ".." is literal, matching what the
// \\?\ namespace stores. GetFullPathNameW cannot be used for these
// spellings because it rewrites the leaf name (#1283). Mirrors the
// product's native_path::to_extended_path_preserving.
inline std::wstring extended_path_preserving(const std::wstring &path) {
  std::wstring native = path;
  for (auto &ch : native) {
    if (ch == L'/') ch = L'\\';
  }

  std::wstring base;
  std::wstring_view tail;
  if (native.size() >= 2 && native[0] == L'\\' && native[1] == L'\\') {
    // UNC absolute: absolutization is identity for the root part.
    base = native;
  } else if (!native.empty() && native[0] == L'\\') {
    // Rooted on the current drive: keep only its root as the base.
    std::vector<std::wstring_view> ignored;
    base = split_root(current_directory(), ignored);
    tail = std::wstring_view(native).substr(1);
  } else if (native.size() > 2 && native[1] == L':' && native[2] == L'\\') {
    base = native.substr(0, 3);
    tail = std::wstring_view(native).substr(3);
  } else if (native.size() >= 2 && native[1] == L':') {
    // Drive-relative "D:rel": resolve against the drive's own cwd.
    std::wstring probe;
    probe.push_back(native[0]);
    probe.append(L":.");
    wchar_t abs_buf[MAX_PATH * 4];
    const DWORD len = GetFullPathNameW(
        probe.c_str(), static_cast<DWORD>(std::size(abs_buf)), abs_buf,
        nullptr);
    base = len != 0 && len < std::size(abs_buf) ? std::wstring(abs_buf, len)
                                                : std::move(probe);
    tail = std::wstring_view(native).substr(2);
  } else {
    base = current_directory();
    tail = native;
  }

  std::vector<std::wstring_view> components;
  const std::wstring root = split_root(base, components);

  size_t start = 0;
  for (;;) {
    const size_t end = tail.find(L'\\', start);
    const std::wstring_view component = tail.substr(
        start, end == std::wstring_view::npos ? std::wstring_view::npos
                                              : end - start);
    if (component == L"..") {
      if (!components.empty()) components.pop_back();
    } else if (!component.empty() && component != L".") {
      components.push_back(component);
    }
    if (end == std::wstring::npos) break;
    start = end + 1;
  }

  std::wstring absolute = root;
  for (size_t i = 0; i < components.size(); ++i) {
    if (i != 0) absolute.push_back(L'\\');
    absolute.append(components[i]);
  }

  if (absolute.size() == 3 && absolute[1] == L':') return absolute;
  if (absolute.size() >= 2 && absolute.compare(0, 2, L"\\\\") == 0) {
    return L"\\\\?\\UNC\\" + absolute.substr(2);
  }
  return L"\\\\?\\" + absolute;
}

inline std::wstring extended_path(const std::filesystem::path &path) {
  auto native = path.wstring();
  if (native.rfind(L"\\\\?\\", 0) == 0) return native;

  // GetFullPathNameW strips trailing '.'/' ' components onto the DOS
  // sibling (#1283), so names in that family are absolutized lexically
  // instead; every other spelling keeps the API result unchanged.
  if (has_literal_dot_space_component(native)) {
    return extended_path_preserving(native);
  }

  wchar_t absolute[MAX_PATH * 4];
  DWORD len =
      GetFullPathNameW(native.c_str(), static_cast<DWORD>(std::size(absolute)),
                       absolute, nullptr);
  if (len == 0 || len >= std::size(absolute)) return native;

  std::wstring out(absolute, len);
  if (out.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + out.substr(2);
  return L"\\\\?\\" + out;
}

// create_directories through the same boundary write_all/read_all use:
// the plain DOS spelling would strip a literal dot/space directory
// component and create the sibling instead, so the directory tree would
// not match the extended spelling the file operations then target.
inline void create_directories(const std::filesystem::path &path) {
  if (has_literal_dot_space_component(path.wstring())) {
    std::filesystem::create_directories(
        std::filesystem::path(extended_path(path)));
  } else {
    std::filesystem::create_directories(path);
  }
}

inline void write_all(const std::filesystem::path &path, const char *data,
                      size_t size) {
  auto extended = extended_path(path);
  HANDLE file = CreateFileW(extended.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;

  DWORD written = 0;
  if (size > 0) {
    WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr);
  }
  CloseHandle(file);
}

inline std::string read_all(const std::filesystem::path &path) {
  auto extended = extended_path(path);
  HANDLE file =
      CreateFileW(extended.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};

  std::string out;
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
    out.append(buffer, buffer + read);
  }
  CloseHandle(file);
  return out;
}

inline DWORD attributes(const std::filesystem::path &path) {
  return GetFileAttributesW(path.wstring().c_str());
}

inline bool set_attributes(const std::filesystem::path &path, DWORD attrs) {
  return SetFileAttributesW(path.wstring().c_str(), attrs) != FALSE;
}

}  // namespace temp_dir_detail

/**
 * @brief Temporary directory management for tests
 *
 * RAII wrapper for temporary directories that automatically
 * creates a unique temporary directory and cleans it up
 * when destroyed. Provides convenient methods for file I/O
 * within the temporary directory.
 */
struct TempDir {
  std::filesystem::path path;  ///< Path to the temporary directory

  /**
   * @brief Create a new temporary directory
   *
   * Uses Windows API to generate a unique temporary directory
   * name and creates the directory structure.
   */
  TempDir() {
    wchar_t base[MAX_PATH];
    // Get system temporary directory path
    GetTempPathW(MAX_PATH, base);

    // Claim the directory name atomically with CreateDirectoryW.  The old
    // GetTempFileNameW + DeleteFileW + create_directory sequence left a
    // window where a parallel ctest worker could claim the same path, so
    // two tests shared (and deleted) each other's files - and a leftover
    // file at that name made create_directory throw out of the before-hook,
    // terminating the test process (0xc0000409).  See #1245/#1260.
    static std::atomic<unsigned long> counter{0};
    const unsigned long pid = GetCurrentProcessId();
    const unsigned long long tick = GetTickCount64();
    for (int attempt = 0; attempt < 100; ++attempt) {
      wchar_t name[MAX_PATH];
      swprintf(name, std::size(name), L"%swct-%lu-%llu-%lu", base, pid, tick,
               counter.fetch_add(1, std::memory_order_relaxed));
      path = name;
      std::error_code ec;
      if (std::filesystem::create_directory(path, ec)) break;
      // !ec means the name already existed: try the next unique name.
    }
  }

  /**
   * @brief Automatically clean up temporary directory
   *
   * Recursively removes all files and subdirectories
   * in the temporary directory when object is destroyed.
   */
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);  // Ignore errors during cleanup
  }

  /**
   * @brief Get wide string path to temporary directory
   *
   * @return std::wstring Wide string representation of directory path
   */
  [[nodiscard]]
  std::wstring wpath() const {
    return path.wstring();
  }

  /**
   * @brief Write text content to a file in the temporary directory
   *
   * Creates parent directories as needed and writes the content
   * in binary mode to preserve exact bytes.
   *
   * @param rel Relative path within temporary directory
   * @param content Text content to write
   */
  void write(const std::string &rel, const std::string &content) const {
    auto p = path / rel;
    temp_dir_detail::create_directories(p.parent_path());
    temp_dir_detail::write_all(p, content.data(), content.size());
  }

  /**
   * @brief Write text content and return the created file path
   *
   * Convenience wrapper for tests that need to pass the path to a command.
   *
   * @param rel Relative path within temporary directory
   * @param content Text content to write
   * @return std::filesystem::path Absolute path to the created file
   */
  [[nodiscard]]
  std::filesystem::path write_file(const std::string &rel,
                                   const std::string &content) const {
    write(rel, content);
    return path / rel;
  }

  /**
   * @brief Write binary data to a file in the temporary directory
   *
   * Creates parent directories as needed and writes raw bytes.
   *
   * @param rel Relative path within temporary directory
   * @param data Vector of bytes to write
   */
  void write_bytes(const std::string &rel,
                   const std::vector<char> &data) const {
    auto p = path / rel;
    temp_dir_detail::create_directories(p.parent_path());
    temp_dir_detail::write_all(p, data.data(), data.size());
  }

  /**
   * @brief Write binary content and return the created file path
   *
   * Convenience wrapper for tests that need to pass the path to a command.
   *
   * @param rel Relative path within temporary directory
   * @param data Raw bytes to write
   * @return std::filesystem::path Absolute path to the created file
   */
  [[nodiscard]]
  std::filesystem::path write_bytes_file(const std::string &rel,
                                         const std::vector<char> &data) const {
    write_bytes(rel, data);
    return path / rel;
  }

  /**
   * @brief Read text content from a file in the temporary directory
   *
   * Reads the entire file content as a string using binary mode
   * to preserve exact bytes.
   *
   * @param rel Relative path within temporary directory
   * @return std::string File content
   */
  std::string read(const std::string &rel) const {
    auto p = path / rel;
    return temp_dir_detail::read_all(p);
  }

  /**
   * @brief Create a subdirectory within the temporary directory
   *
   * Creates a directory at the specified relative path, creating
   * parent directories as needed.
   *
   * @param rel Relative path of the directory to create
   */
  void mkdir(const std::string &rel) const {
    auto p = path / rel;
    temp_dir_detail::create_directories(p);
  }

  [[nodiscard]]
  DWORD attrs(const std::string &rel) const {
    return temp_dir_detail::attributes(path / rel);
  }

  bool set_attrs(const std::string &rel, DWORD attrs) const {
    return temp_dir_detail::set_attributes(path / rel, attrs);
  }

  bool add_attrs(const std::string &rel, DWORD bits) const {
    DWORD current = attrs(rel);
    if (current == INVALID_FILE_ATTRIBUTES) return false;
    return set_attrs(rel, current | bits);
  }

  bool clear_attrs(const std::string &rel, DWORD bits) const {
    DWORD current = attrs(rel);
    if (current == INVALID_FILE_ATTRIBUTES) return false;
    return set_attrs(rel, current & ~bits);
  }
};
