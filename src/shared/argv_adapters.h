// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
// Single source of truth for the argv adapters: opt/ payloads that need a
// fixed argv prefix to match the rest of the command layer
// (unixwin/niubash#467).
//
// ezwinports gawk is a text-mode mingw build - its CRT turns every \n into
// \r\n on write and folds \r\n into \n on read, while every winuxcmd applet
// does binary stdio. The build carries gawk's BINMODE feature (it ignores
// the BINMODE environment variable but honors `-v BINMODE=3`), so the
// switch moves stdin/stdout/stderr - and files the program opens for
// writing - to binary and the bytes match GNU awk. The prefix goes ahead
// of the caller's arguments so an explicit `-v BINMODE=...` still wins:
// later assignments override earlier ones.
//
// Two binaries consume this table and cannot share a module: the package
// shim (src/shims/winuxcmd_shim.cpp, a standalone CRT-only forwarder) and
// the multiplexer (src/Main/main.cpp, the `winuxcmd <verb>` forward to an
// opt/ payload). Each used to keep its own hand-copied copy of the rule,
// in a different character width, kept in sync by a comment; this header
// is the only copy now.
#pragma once

#include <string_view>

namespace argv_adapters {

struct Entry {
  std::string_view command;  // ASCII payload name, matched case-insensitively
  std::wstring_view prefix;  // spliced ahead of the caller's arguments
};

inline constexpr Entry kTable[] = {
    {"awk", L" -v BINMODE=3"},
    {"gawk", L" -v BINMODE=3"},
};

inline bool name_equals(std::string_view name,
                        std::string_view literal) noexcept {
  if (name.size() != literal.size()) return false;
  for (size_t i = 0; i < name.size(); ++i) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != literal[i]) return false;
  }
  return true;
}

inline bool name_equals(std::wstring_view name,
                        std::string_view literal) noexcept {
  if (name.size() != literal.size()) return false;
  for (size_t i = 0; i < name.size(); ++i) {
    wchar_t c = name[i];
    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    if (c != static_cast<wchar_t>(literal[i])) return false;
  }
  return true;
}

inline std::wstring_view prefix_for(std::string_view command) noexcept {
  for (const auto& entry : kTable) {
    if (name_equals(command, entry.command)) return entry.prefix;
  }
  return {};
}

inline std::wstring_view prefix_for(std::wstring_view command) noexcept {
  for (const auto& entry : kTable) {
    if (name_equals(command, entry.command)) return entry.prefix;
  }
  return {};
}

}  // namespace argv_adapters
