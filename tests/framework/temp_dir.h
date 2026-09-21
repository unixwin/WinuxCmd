// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#pragma once
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace temp_dir_detail {

inline std::wstring extended_path(const std::filesystem::path &path) {
  auto native = path.wstring();
  if (native.rfind(L"\\\\?\\", 0) == 0) return native;

  wchar_t absolute[MAX_PATH * 4];
  DWORD len =
      GetFullPathNameW(native.c_str(), static_cast<DWORD>(std::size(absolute)),
                       absolute, nullptr);
  if (len == 0 || len >= std::size(absolute)) return native;

  std::wstring out(absolute, len);
  if (out.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + out.substr(2);
  return L"\\\\?\\" + out;
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

    wchar_t name[MAX_PATH];
    // Generate unique temporary file name (we'll use it as directory name)
    GetTempFileNameW(base, L"wct", 0, name);
    DeleteFileW(name);  // Remove the temporary file

    path = name;
    std::filesystem::create_directory(path);  // Create directory instead
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
    std::filesystem::create_directories(p.parent_path());
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
    std::filesystem::create_directories(p.parent_path());
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
    std::filesystem::create_directories(p);
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
