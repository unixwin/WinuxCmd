// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module utils:path;

import std;
import :utf8;
import :native_path;

// This partition used to re-export the whole native_path surface under
// shorter names (normalize_path, get_parent_path, get_filename, ...).
// Every one of those wrappers had zero call sites - the tree calls
// native_path:: directly - so they were removed; the single live export
// below is the only reason the partition remains.
namespace path {

export std::string get_executable_name(const char* argv0) {
  auto executable =
      std::filesystem::path(native_path::from_utf8(argv0 ? argv0 : ""));
  return native_path::to_utf8(executable.stem().wstring());
}

}  // namespace path
