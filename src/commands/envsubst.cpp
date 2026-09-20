// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for envsubst.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr ENVSUBST_OPTIONS = std::array{
    // [GNU]
    OPTION("-v", "--variables", "output variables occurring in SHELL-FORMAT")};

namespace envsubst_pipeline {

auto is_var_start(char ch) -> bool {
  return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

auto is_var_char(char ch) -> bool {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

auto extract_variables(std::string_view text) -> std::set<std::string> {
  std::set<std::string> vars;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '$' || i + 1 >= text.size()) continue;

    if (text[i + 1] == '{') {
      size_t name_start = i + 2;
      if (name_start >= text.size() || !is_var_start(text[name_start])) {
        continue;
      }
      size_t pos = name_start + 1;
      while (pos < text.size() && is_var_char(text[pos])) ++pos;
      if (pos < text.size() && text[pos] == '}') {
        vars.insert(std::string(text.substr(name_start, pos - name_start)));
        i = pos;
      }
      continue;
    }

    if (!is_var_start(text[i + 1])) continue;
    size_t name_start = i + 1;
    size_t pos = name_start + 1;
    while (pos < text.size() && is_var_char(text[pos])) ++pos;
    vars.insert(std::string(text.substr(name_start, pos - name_start)));
    i = pos - 1;
  }
  return vars;
}

auto get_env_value(const std::string& name) -> std::string {
  std::wstring wname = utf8_to_wstring(name);
  DWORD needed = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
  if (needed == 0) return {};

  std::wstring value(needed, L'\0');
  DWORD written = GetEnvironmentVariableW(wname.c_str(), value.data(), needed);
  if (written == 0 || written >= needed) return {};
  value.resize(written);
  return wstring_to_utf8(value);
}

auto should_replace(std::string_view name,
                    const std::optional<std::set<std::string>>& allowlist)
    -> bool {
  if (!allowlist) return true;
  return allowlist->contains(std::string(name));
}

auto substitute(std::string_view input,
                const std::optional<std::set<std::string>>& allowlist)
    -> std::string {
  std::string out;
  out.reserve(input.size());

  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] != '$' || i + 1 >= input.size()) {
      out.push_back(input[i]);
      continue;
    }

    if (input[i + 1] == '{') {
      size_t name_start = i + 2;
      if (name_start >= input.size() || !is_var_start(input[name_start])) {
        out.push_back(input[i]);
        continue;
      }
      size_t pos = name_start + 1;
      while (pos < input.size() && is_var_char(input[pos])) ++pos;
      if (pos >= input.size() || input[pos] != '}') {
        out.push_back(input[i]);
        continue;
      }

      auto name = input.substr(name_start, pos - name_start);
      if (should_replace(name, allowlist)) {
        out += get_env_value(std::string(name));
      } else {
        out.append(input.substr(i, pos - i + 1));
      }
      i = pos;
      continue;
    }

    if (!is_var_start(input[i + 1])) {
      out.push_back(input[i]);
      continue;
    }
    size_t name_start = i + 1;
    size_t pos = name_start + 1;
    while (pos < input.size() && is_var_char(input[pos])) ++pos;
    auto name = input.substr(name_start, pos - name_start);
    if (should_replace(name, allowlist)) {
      out += get_env_value(std::string(name));
    } else {
      out.append(input.substr(i, pos - i));
    }
    i = pos - 1;
  }

  return out;
}

auto run(const CommandContext<ENVSUBST_OPTIONS.size()>& ctx) -> int {
  bool variables_only =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--variables", false);
  if (ctx.positionals.size() > 1) {
    safeErrorPrintLn("envsubst: too many arguments");
    safeErrorPrintLn("Try 'envsubst --help' for more information.");
    return 1;
  }

  std::optional<std::set<std::string>> allowlist;
  if (!ctx.positionals.empty()) {
    allowlist = extract_variables(ctx.positionals.front());
  }

  if (variables_only) {
    if (!allowlist) {
      safeErrorPrintLn("envsubst: missing SHELL-FORMAT for --variables");
      return 1;
    }
    for (const auto& name : *allowlist) safePrintLn(name);
    return 0;
  }

  auto input = file_io::read_all_stdin();
  if (!input) {
    safeErrorPrintLn("envsubst: " + input.error());
    return 1;
  }
  safePrint(substitute(*input, allowlist));
  return 0;
}

}  // namespace envsubst_pipeline

REGISTER_COMMAND(envsubst, "envsubst", "envsubst [OPTION] [SHELL-FORMAT]",
                 "Substitute environment variables in standard input.",
                 "  echo 'hello $USER' | envsubst\n"
                 "  envsubst '$HOME ${USER}' < template.txt",
                 "env(1), printenv(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 ENVSUBST_OPTIONS) {
  return envsubst_pipeline::run(ctx);
}
