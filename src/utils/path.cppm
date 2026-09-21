// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module utils:path;

import std;
import :utf8;
import :native_path;

namespace path {

export std::string normalize_path(const std::string& path) {
  return native_path::normalize_separators_utf8(path);
}

export std::wstring normalize_path(const std::wstring& path) {
  return native_path::normalize_separators(path);
}

export std::string get_parent_path(const std::string& path) {
  return native_path::parent_path(path);
}

export std::wstring get_parent_path(const std::wstring& path) {
  return native_path::parent_path_w(path);
}

export std::string get_filename(const std::string& path) {
  return native_path::filename(path);
}

export std::wstring get_filename(const std::wstring& path) {
  return native_path::filename_w(path);
}

export std::string get_stem(const std::string& path) {
  return native_path::stem(path);
}

export std::wstring get_stem(const std::wstring& path) {
  return native_path::stem_w(path);
}

export std::string get_extension(const std::string& path) {
  return native_path::extension(path);
}

export std::wstring get_extension(const std::wstring& path) {
  return native_path::extension_w(path);
}

export bool exists(const std::string& path) {
  return native_path::exists(path);
}

export bool exists(const std::wstring& path) {
  return native_path::exists_w(path);
}

export bool is_directory(const std::string& path) {
  return native_path::is_directory(path);
}

export bool is_directory(const std::wstring& path) {
  return native_path::is_directory_w(path);
}

export bool is_regular_file(const std::string& path) {
  return native_path::is_regular_file(path);
}

export bool is_regular_file(const std::wstring& path) {
  return native_path::is_regular_file_w(path);
}

export bool create_directory(const std::string& path) {
  return native_path::create_directory(path);
}

export bool create_directory(const std::wstring& path) {
  return native_path::create_directory_w(path);
}

export bool create_directories(const std::string& path) {
  return native_path::create_directories(path);
}

export bool create_directories(const std::wstring& path) {
  return native_path::create_directories_w(path);
}

export std::string current_path() { return native_path::current_directory(); }

export std::wstring current_path_w() {
  return native_path::current_directory_w();
}

export std::string get_executable_path(const char* argv0) {
  return native_path::to_utf8(native_path::from_utf8(argv0 ? argv0 : ""));
}

export std::string get_executable_name(const char* argv0) {
  auto executable =
      std::filesystem::path(native_path::from_utf8(argv0 ? argv0 : ""));
  return native_path::to_utf8(executable.stem().wstring());
}

export std::string join(const std::string& base, const std::string& relative) {
  return native_path::join(base, relative);
}

export std::wstring join(const std::wstring& base,
                         const std::wstring& relative) {
  return native_path::join_w(base, relative);
}

}  // namespace path
