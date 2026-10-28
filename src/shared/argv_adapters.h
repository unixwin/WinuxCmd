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
// Consumers that cannot share a module: the package shim
// (src/shims/winuxcmd_shim.cpp, a standalone CRT-only forwarder), the
// multiplexer (src/Main/main.cpp, the `winuxcmd <verb>` forward to an
// opt/ payload), and wpm's entry-link routing (src/commands/wpm.cpp).
// Each used to keep its own hand-copied spelling of this rule, in
// different character widths, kept in sync by comments; this header is
// the only copy now.
//
// The API is deliberately plain C types (no std:: names): command TUs in
// this tree take their std from the PCH + `import std`, and a textual
// <string_view> include inside the unity-built commands TU redefines the
// std module's entities. Callers pass .data()/.size() of their
// string_view/wstring and wrap the returned pointer as they see fit.
#pragma once

#include <stddef.h>

namespace argv_adapters {

struct Entry {
  const char* command;     // ASCII payload name, matched case-insensitively
  const wchar_t* prefix;   // spliced ahead of the caller's arguments
};

inline constexpr Entry kTable[] = {
    {"awk", L" -v BINMODE=3"},
    {"gawk", L" -v BINMODE=3"},
};

inline bool name_equals(const char* name, size_t name_len,
                        const char* literal) noexcept {
  size_t i = 0;
  for (; literal[i] != '\0'; ++i) {
    if (i >= name_len) return false;
    char c = name[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != literal[i]) return false;
  }
  return i == name_len;
}

inline bool name_equals(const wchar_t* name, size_t name_len,
                        const char* literal) noexcept {
  size_t i = 0;
  for (; literal[i] != '\0'; ++i) {
    if (i >= name_len) return false;
    wchar_t c = name[i];
    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    if (c != static_cast<wchar_t>(literal[i])) return false;
  }
  return i == name_len;
}

// The argv prefix for a payload name, or nullptr when the payload is not
// adapted. Narrow overload takes the UTF-8 name, wide overload the UTF-16
// one; matching is ASCII case-insensitive either way.
inline const wchar_t* prefix_for(const char* name, size_t name_len) noexcept {
  for (const auto& entry : kTable) {
    if (name_equals(name, name_len, entry.command)) return entry.prefix;
  }
  return nullptr;
}

inline const wchar_t* prefix_for(const wchar_t* name,
                                 size_t name_len) noexcept {
  for (const auto& entry : kTable) {
    if (name_equals(name, name_len, entry.command)) return entry.prefix;
  }
  return nullptr;
}

}  // namespace argv_adapters
