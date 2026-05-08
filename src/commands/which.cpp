/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implemention for which.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"
import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr WHICH_OPTIONS = std::array{
    OPTION("-a", "--all", "print all matching pathnames of each argument"),
    OPTION("-s", "--skip-dot",
           "skip directories in PATH that start with a dot [NOT SUPPORT]"),
    OPTION("", "--skip-tilde",
           "skip directories in PATH that start with a tilde [NOT SUPPORT]"),
    OPTION("", "--show-dot",
           "if a directory in PATH starts with a dot, print it [NOT SUPPORT]"),
    OPTION("", "--show-tilde",
           "output a tilde for HOME directory [NOT SUPPORT]")};

namespace which_pipeline {
namespace cp = core::pipeline;
namespace fs = std::filesystem;

struct Config {
  bool all = false;
  SmallVector<std::string, 32> names;
};

auto split_semicolon(std::string_view text) -> std::vector<std::string> {
  SmallVector<std::string, 256> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t pos = text.find(';', start);
    if (pos == std::string_view::npos) {
      out.emplace_back(text.substr(start));
      break;
    }
    out.emplace_back(text.substr(start, pos - start));
    start = pos + 1;
  }
  return std::vector<std::string>(out.begin(), out.end());
}

auto get_env_utf8(const wchar_t* key) -> std::optional<std::string> {
  DWORD size = GetEnvironmentVariableW(key, nullptr, 0);
  if (size == 0) return std::nullopt;

  std::wstring value;
  value.resize(size - 1);
  if (GetEnvironmentVariableW(key, value.data(), size) == 0) {
    return std::nullopt;
  }
  return wstring_to_utf8(value);
}

auto get_path_entries() -> std::vector<std::string> {
  auto path_env = get_env_utf8(L"PATH");
  if (!path_env.has_value() || path_env->empty()) return {};
  return split_semicolon(*path_env);
}

auto get_pathext_entries() -> std::vector<std::string> {
  auto ext_env = get_env_utf8(L"PATHEXT");
  if (!ext_env.has_value() || ext_env->empty()) {
    return std::vector<std::string>{".exe", ".cmd", ".bat", ".com"};
  }
  auto exts = split_semicolon(*ext_env);
  if (exts.empty())
    exts = std::vector<std::string>{".exe", ".cmd", ".bat", ".com"};
  for (auto& ext : exts) {
    if (!ext.empty() && ext.front() != '.') ext.insert(ext.begin(), '.');
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
  }
  return exts;
}

auto has_path_separator(std::string_view s) -> bool {
  return s.find('/') != std::string_view::npos ||
         s.find('\\') != std::string_view::npos;
}

auto exists_regular(const fs::path& p) -> bool {
  std::error_code ec;
  return fs::exists(p, ec) && fs::is_regular_file(p, ec);
}

auto with_extensions(const fs::path& base, const std::vector<std::string>& exts)
    -> std::vector<fs::path> {
  std::vector<fs::path> out;
  out.push_back(base);
  if (base.has_extension()) return out;

  for (const auto& ext : exts) {
    fs::path p = base;
    p += ext;
    out.push_back(std::move(p));
  }
  return out;
}

auto find_one(std::string_view name, bool all) -> std::vector<std::string> {
  SmallVector<std::string, 64> hits;
  std::unordered_set<std::string> seen;
  const auto pathext = get_pathext_entries();

  auto append_hit = [&](const fs::path& p) {
    auto display = p.generic_string();
    if (seen.insert(display).second) {
      hits.push_back(std::move(display));
    }
  };

  if (has_path_separator(name)) {
    fs::path base = std::string(name);
    for (const auto& candidate : with_extensions(base, pathext)) {
      if (exists_regular(candidate)) {
        append_hit(candidate);
        if (!all) return std::vector<std::string>(hits.begin(), hits.end());
      }
    }
    return std::vector<std::string>(hits.begin(), hits.end());
  }

  for (const auto& dir : get_path_entries()) {
    if (dir.empty()) continue;
    fs::path base = fs::path(dir) / std::string(name);
    for (const auto& candidate : with_extensions(base, pathext)) {
      if (exists_regular(candidate)) {
        append_hit(candidate);
        if (!all) return std::vector<std::string>(hits.begin(), hits.end());
      }
    }
  }

  return std::vector<std::string>(hits.begin(), hits.end());
}

auto is_unsupported_used(const CommandContext<WHICH_OPTIONS.size()>& ctx)
    -> std::optional<std::string_view> {
  if (ctx.get<bool>("--skip-dot", false) || ctx.get<bool>("-s", false))
    return "--skip-dot is [NOT SUPPORT]";
  if (ctx.get<bool>("--skip-tilde", false))
    return "--skip-tilde is [NOT SUPPORT]";
  if (ctx.get<bool>("--show-dot", false)) return "--show-dot is [NOT SUPPORT]";
  if (ctx.get<bool>("--show-tilde", false))
    return "--show-tilde is [NOT SUPPORT]";
  return std::nullopt;
}

auto build_config(const CommandContext<WHICH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  for (auto arg : ctx.positionals) cfg.names.emplace_back(arg);
  if (cfg.names.empty()) return std::unexpected("missing command operand");
  return cfg;
}

auto run(const Config& cfg) -> int {
  bool all_found = true;
  for (const auto& name : cfg.names) {
    auto hits = find_one(name, cfg.all);
    if (hits.empty()) {
      all_found = false;
      continue;
    }
    for (const auto& h : hits) {
      safePrint(h);
      safePrint("\n");
    }
  }
  return all_found ? 0 : 1;
}

}  // namespace which_pipeline

REGISTER_COMMAND(which, "which", "which [OPTION]... COMMAND...",
                 "Locate COMMAND in PATH.",
                 "  which ls\n"
                 "  which -a python",
                 "where(1), command(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", WHICH_OPTIONS) {
  using namespace which_pipeline;

  if (auto unsupported = is_unsupported_used(ctx); unsupported.has_value()) {
    cp::report_custom_error(L"which", utf8_to_wstring(*unsupported));
    return 2;
  }

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"which");
    return 1;
  }
  return run(*cfg);
}
