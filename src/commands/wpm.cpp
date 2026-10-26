// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Winux Package Manager internal command.
/// @Version: 0.2.0
/// @License: MIT

#include <bcrypt.h>
#include <tlhelp32.h>
#include <urlmon.h>
#include <winhttp.h>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "winhttp.lib")

import std;
import core;
import utils;
import version;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr WPM_OPTIONS = std::array{
    // [EXT] option
    OPTION("-r", "--root", "manage a specific WinuxCmd root", STRING_TYPE),
    // [EXT] option
    OPTION("-s", "--source", "use a specific index source", STRING_TYPE),
    // [EXT] option
    OPTION("-a", "--all", "show index-only packages in list output"),
    // [EXT] option
    OPTION("-f", "--force", "overwrite existing files when safe"),
    // [EXT] option
    OPTION("-n", "--dry-run", "show planned changes without writing"),
    // [EXT] option
    OPTION("-v", "--verbose", "print detailed progress"),
    // [EXT] option
    OPTION("-y", "--yes", "assume yes for interactive prompts"),
    // [EXT] option
    OPTION("", "--category", "filter list/search output by category",
           STRING_TYPE),
    // [EXT] option
    OPTION("", "--json", "print machine-readable JSON"),
    // [EXT] option
    OPTION("", "--plain", "print only package names for export"),
    // [EXT] option
    OPTION("", "--proxy", "use the given HTTP proxy for downloads",
           STRING_TYPE),
    // [EXT] option
    OPTION("", "--from", "install packages from a manifest file or URL",
           STRING_TYPE)};

namespace wpm {
namespace fs = std::filesystem;

constexpr std::string_view kVersion = "0.4.0";
constexpr std::string_view kInternalToolName = "wpm";

template <typename... Args>
auto wpm_text(std::string_view key, std::string_view fallback, Args&&... args)
    -> std::string {
  return winux::i18n::format(key, fallback, std::forward<Args>(args)...);
}

constexpr std::string_view kBuiltinIndex = R"json(
{
  "schema": 1,
  "name": "official",
  "version": "builtin-2026.10.20",
  "updated": "2026-10-20",
  "sources": [
    {
      "name": "official-github-raw",
      "region": "global",
      "priority": 10,
      "description": "Canonical raw GitHub index for the official WinuxCmd WPM source.",
      "homepage": "https://github.com/unixwin/wpm-source",
      "index_urls": [
        "https://raw.githubusercontent.com/unixwin/wpm-source/main/index.json"
      ]
    },
    {
      "name": "official-jsdelivr",
      "region": "global",
      "priority": 15,
      "description": "CDN mirror of the canonical WPM index.",
      "homepage": "https://cdn.jsdelivr.net/gh/unixwin/wpm-source@main/index.json",
      "index_urls": [
        "https://cdn.jsdelivr.net/gh/unixwin/wpm-source@main/index.json"
      ]
    },
    {
      "name": "official-github-release",
      "region": "global",
      "priority": 20,
      "description": "Release-pinned WPM index for stable WinuxCmd packages.",
      "homepage": "https://github.com/unixwin/wpm-source/releases",
      "index_urls": [
        "https://github.com/unixwin/wpm-source/releases/latest/download/wpm-index.json"
      ]
    },
    {
      "name": "official-cn",
      "region": "cn",
      "priority": 30,
      "description": "China mirror index served via gh.caomengxuan666.com; artifact URLs point at the nginx cache proxy, GitHub origins kept as fallback.",
      "homepage": "https://gh.caomengxuan666.com/wpm/index.json",
      "index_urls": [
        "https://gh.caomengxuan666.com/wpm/index.json"
      ]
    }
  ],
  "packages": []
}
)json";

struct HttpResult {
  bool ok = false;
  std::vector<std::byte> data;
  DWORD status = 0;
  std::string error;
};

struct Options {
  fs::path root;
  std::string source;
  std::string category;
  std::string proxy;
  std::string from;
  bool all = false;
  bool force = false;
  bool dry_run = false;
  bool verbose = false;
  bool json = false;
  bool plain = false;
  bool yes = false;
};

struct FileId {
  DWORD volume = 0;
  DWORD high = 0;
  DWORD low = 0;
};

auto lower_ascii(std::string s) -> std::string {
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

auto starts_with_ci(std::string_view text, std::string_view prefix) -> bool {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(text[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

auto exe_suffix(std::string_view name) -> std::string {
  std::string out(name);
  if (!out.ends_with(".exe")) out += ".exe";
  return out;
}

auto current_exe_path() -> fs::path {
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                  static_cast<DWORD>(buffer.size()));
  while (size == buffer.size()) {
    buffer.resize(buffer.size() * 2, L'\0');
    size = GetModuleFileNameW(nullptr, buffer.data(),
                              static_cast<DWORD>(buffer.size()));
  }
  buffer.resize(size);
  return fs::path(buffer);
}

auto default_root() -> fs::path {
  auto exe_dir = current_exe_path().parent_path();
  if (exe_dir.filename() == L"bin") {
    auto parent = exe_dir.parent_path();
    if (parent.filename() == L"usr") return parent.parent_path();
    if (parent.filename() == L"local" &&
        parent.parent_path().filename() == L"usr") {
      return parent.parent_path().parent_path();
    }
    return parent;
  }
  return exe_dir.empty() ? fs::current_path() : exe_dir;
}

auto canonical_bin_dir(const fs::path& root) -> fs::path {
  return root / "usr" / "bin";
}

auto canonical_winuxcmd_path(const fs::path& root) -> fs::path {
  return canonical_bin_dir(root) / "winuxcmd.exe";
}

// /tmp is deliberately absent: it is a per-user namespace backed by the real
// Windows temp directory (unixwin/niubash#94), never the install tree, so no
// <root>\tmp is created or consulted anywhere.
auto ensure_install_layout(const fs::path& root) -> bool {
  std::error_code ec;
  for (const auto* relative :
       {"bin", "usr/bin", "usr/local/bin", "etc", "var", "dev", "opt"}) {
    fs::create_directories(root / relative, ec);
    if (ec) return false;
  }
  return true;
}

auto normalized_package_target(std::string target, bool directory) -> fs::path {
  std::ranges::replace(target, '\\', '/');
  while (!target.empty() && target.front() == '/') target.erase(target.begin());
  if (!directory && target.find('/') == std::string::npos) {
    return fs::path("usr") / "bin" / target;
  }
  return fs::path(target);
}

// Shim-layout packages keep their payload (exe + private DLLs) self-contained
// under <root>\opt\<pkg>\; usr\bin only gets wpm-shim forwarders. Flat layout
// (default) installs mapped files directly into usr\bin as before.
auto artifact_layout(const nlohmann::json& artifact) -> std::string {
  const std::string layout = artifact.value("layout", "flat");
  return layout == "shim" ? "shim" : "flat";
}

auto package_payload_dir(const fs::path& root, std::string_view package)
    -> fs::path {
  return root / "opt" / fs::path(std::string(package));
}

auto state_dir(const fs::path& root) -> fs::path { return root / ".wpm"; }
auto cache_dir(const fs::path& root) -> fs::path {
  return state_dir(root) / "cache";
}
auto index_dir(const fs::path& root) -> fs::path {
  return state_dir(root) / "indexes";
}
auto staging_dir(const fs::path& root) -> fs::path {
  return state_dir(root) / "staging";
}
auto backup_dir(const fs::path& root) -> fs::path {
  return state_dir(root) / "backup";
}
auto config_path(const fs::path& root) -> fs::path {
  return state_dir(root) / "config.json";
}
auto local_index_path(const fs::path& root) -> fs::path {
  return index_dir(root) / "official.json";
}
auto read_text(const fs::path& path) -> std::optional<std::string> {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

auto write_text(const fs::path& path, std::string_view text) -> bool {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return out.good();
}

/// The package-shim forwarder (`winuxcmd-shim.exe`) reads its payload from
/// this map: one line per command, `<command>=<payload relative to the
/// installation root>`. Shims used to be hardlinks to winuxcmd.exe, which spent
/// a whole WinuxCmd startup dispatching to `opt\<pkg>\<command>.exe`
/// (unixwin/WinuxCmd#1207).
auto shim_map_path(const fs::path& root) -> fs::path {
  return state_dir(root) / "shims.map";
}

auto read_shim_map(const fs::path& root)
    -> std::vector<std::pair<std::string, std::string>> {
  std::vector<std::pair<std::string, std::string>> entries;
  auto text = read_text(shim_map_path(root));
  if (!text) return entries;
  const std::string_view view(*text);
  size_t position = 0;
  while (position <= view.size()) {
    const size_t end = view.find('\n', position);
    std::string_view line = view.substr(
        position, end == std::string_view::npos ? std::string_view::npos
                                                : end - position);
    position = end == std::string_view::npos ? view.size() + 1 : end + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t'))
      line.remove_suffix(1);
    if (line.empty() || line.front() == '#') continue;
    const auto equals = line.find('=');
    if (equals == std::string_view::npos || equals == 0) continue;
    entries.emplace_back(std::string(line.substr(0, equals)),
                         std::string(line.substr(equals + 1)));
  }
  return entries;
}

auto write_shim_map(const fs::path& root,
                    const std::vector<std::pair<std::string, std::string>>& entries)
    -> bool {
  std::string text =
      "# Package shim payloads, maintained by wpm: <command>=<payload> where the\n"
      "# payload is relative to this installation root.\n";
  for (const auto& [command, payload] : entries)
    text += command + "=" + payload + "\n";
  return write_text(shim_map_path(root), text);
}

auto set_shim_map_entry(const fs::path& root, const std::string& command,
                        const std::string& payload) -> bool {
  auto entries = read_shim_map(root);
  for (auto& [name, target] : entries) {
    if (name != command) continue;
    if (target == payload) return true;  // already correct
    target = payload;
    return write_shim_map(root, entries);
  }
  entries.emplace_back(command, payload);
  return write_shim_map(root, entries);
}

/// Drop the map entries of these commands (uninstall path).
void remove_shim_map_entries(const fs::path& root,
                             const std::vector<std::string>& commands) {
  auto entries = read_shim_map(root);
  const size_t before = entries.size();
  std::erase_if(entries, [&](const auto& entry) {
    return std::ranges::find(commands, entry.first) != commands.end();
  });
  if (entries.size() != before) write_shim_map(root, entries);
}

/// The forwarder that replaced "spend a winuxcmd startup to spawn the payload",
/// when this installation has it.
auto package_shim_source(const fs::path& root) -> fs::path {
  const fs::path forwarder = canonical_bin_dir(root) / "winuxcmd-shim.exe";
  std::error_code ec;
  return fs::is_regular_file(forwarder, ec) ? forwarder : fs::path{};
}

auto write_bytes(const fs::path& path, std::span<const std::byte> data)
    -> bool {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
  return out.good();
}

auto parse_json_text(std::string_view text) -> std::optional<nlohmann::json> {
  try {
    return nlohmann::json::parse(text.begin(), text.end());
  } catch (const std::exception& e) {
    safeErrorPrintLn(wpm_text("command.wpm.error.invalid_json",
                              "wpm: invalid JSON: {}", e.what()));
    return std::nullopt;
  }
}

auto load_config(const fs::path& root) -> nlohmann::json {
  if (auto text = read_text(config_path(root))) {
    if (auto parsed = parse_json_text(*text)) return *parsed;
  }
  return nlohmann::json{{"preferred_source", "auto"},
                        {"region", "auto"},
                        {"user_sources", nlohmann::json::array()}};
}

auto save_config(const fs::path& root, const nlohmann::json& config) -> bool {
  return write_text(config_path(root), config.dump(2));
}

auto load_index(const fs::path& root) -> nlohmann::json {
  if (auto text = read_text(local_index_path(root))) {
    if (auto parsed = parse_json_text(*text)) return *parsed;
  }
  if (auto parsed = parse_json_text(kBuiltinIndex)) return *parsed;
  return nlohmann::json::object();
}

// Candidate index sources are the built-in registry plus sources the user
// explicitly configured (`wpm source add` or config user_sources). Sources
// declared inside the cached index file are deliberately NOT merged: the
// cached index is itself fetched from those sources, so honoring its
// 'sources' array let one poisoned fetch persistently inject new download
// locations that later fetches would trust (issue #1176). New mirrors must
// be added by the user or shipped in the built-in registry.
auto merged_sources(const fs::path& root) -> std::vector<nlohmann::json> {
  std::vector<nlohmann::json> sources;
  if (auto builtin = parse_json_text(kBuiltinIndex);
      builtin && builtin->contains("sources") &&
      (*builtin)["sources"].is_array()) {
    for (const auto& source : (*builtin)["sources"]) {
      if (!source.is_object()) continue;
      sources.push_back(source);
    }
  }

  auto config = load_config(root);
  if (config.contains("user_sources") && config["user_sources"].is_array()) {
    for (const auto& source : config["user_sources"]) sources.push_back(source);
  }

  std::ranges::sort(
      sources, [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.value("priority", 1000) < b.value("priority", 1000);
      });
  return sources;
}

auto package_array(const nlohmann::json& index) -> std::vector<nlohmann::json> {
  std::vector<nlohmann::json> out;
  if (!index.contains("packages") || !index["packages"].is_array()) return out;
  for (const auto& pkg : index["packages"]) {
    if (pkg.is_object()) out.push_back(pkg);
  }
  return out;
}

auto find_package(const nlohmann::json& index, std::string_view name)
    -> std::optional<nlohmann::json> {
  auto wanted = lower_ascii(std::string(name));
  for (const auto& pkg : package_array(index)) {
    if (lower_ascii(pkg.value("name", "")) == wanted) return pkg;
  }
  return std::nullopt;
}

/// A package may be an alias: an index entry that installs nothing of its own
/// and names the package which actually provides the command. That keeps one
/// name per tool - `git` and `niugit` both claimed `commands: ["git"]`, so an
/// install depended on which entry a user happened to pick, and the same shape
/// exists for `awk`/`gawk` in the index
/// (unixwin/WinuxCmd#1218).
constexpr size_t kMaxPackageAliasDepth = 4;

auto package_alias_target(const nlohmann::json& pkg) -> std::string {
  if (!pkg.contains("alias_of") || !pkg["alias_of"].is_string()) return {};
  return pkg["alias_of"].get<std::string>();
}

struct ResolvedPackageAlias {
  /// The package that installs something.
  std::string name;
  /// Requested name first, target last.
  std::vector<std::string> chain;
  bool cycle = false;
};

auto resolve_package_alias(const nlohmann::json& index,
                           std::string_view requested) -> ResolvedPackageAlias {
  ResolvedPackageAlias resolved{std::string(requested), {std::string(requested)},
                                false};
  for (size_t depth = 0; depth < kMaxPackageAliasDepth; ++depth) {
    auto pkg = find_package(index, resolved.name);
    if (!pkg) break;
    const auto target = package_alias_target(*pkg);
    if (target.empty()) break;
    if (std::ranges::find(resolved.chain, target) != resolved.chain.end()) {
      // Show the closing hop too, so the message reads as the loop it is:
      // loop-a -> loop-b -> loop-a.
      resolved.chain.push_back(target);
      resolved.cycle = true;
      break;
    }
    resolved.name = target;
    resolved.chain.push_back(target);
  }
  return resolved;
}

auto format_package_alias_chain(const std::vector<std::string>& chain)
    -> std::string {
  std::string text;
  for (size_t i = 0; i < chain.size(); ++i) {
    if (i != 0) text += " -> ";
    text += chain[i];
  }
  return text;
}

auto detect_arch_key() -> std::string {
  SYSTEM_INFO info{};
  GetNativeSystemInfo(&info);
  switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_ARM64:
      return "windows-arm64";
    case PROCESSOR_ARCHITECTURE_AMD64:
    default:
      return "windows-x64";
  }
}

auto file_id(const fs::path& path) -> std::optional<FileId> {
  HANDLE h =
      CreateFileW(path.wstring().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return std::nullopt;

  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(h, &info)) {
    CloseHandle(h);
    return std::nullopt;
  }
  CloseHandle(h);
  return FileId{info.dwVolumeSerialNumber, info.nFileIndexHigh,
                info.nFileIndexLow};
}

auto same_file(const fs::path& a, const fs::path& b) -> bool {
  auto fa = file_id(a);
  auto fb = file_id(b);
  return fa && fb && fa->volume == fb->volume && fa->high == fb->high &&
         fa->low == fb->low;
}

auto normalized_path_key(const fs::path& path) -> std::wstring {
  std::error_code ec;
  fs::path normalized = fs::weakly_canonical(path, ec);
  if (ec) {
    ec.clear();
    normalized = fs::absolute(path, ec);
    if (ec) normalized = path;
  }
  normalized = normalized.lexically_normal();

  std::wstring key = normalized.wstring();
  std::ranges::replace(key, L'/', L'\\');
  std::ranges::transform(key, key.begin(),
                         [](wchar_t ch) { return std::towlower(ch); });
  return key;
}

auto same_path_name(const fs::path& a, const fs::path& b) -> bool {
  return normalized_path_key(a) == normalized_path_key(b);
}

auto win32_error_text(DWORD error) -> std::string {
  LPWSTR buffer = nullptr;
  DWORD size = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  if (size == 0 || buffer == nullptr) {
    return std::system_category().message(static_cast<int>(error));
  }
  std::wstring text(buffer, size);
  LocalFree(buffer);
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
                           text.back() == L' ' || text.back() == L'\t')) {
    text.pop_back();
  }
  return wstring_to_utf8(text);
}

auto command_names() -> std::vector<std::string> {
  std::vector<std::string> names;
  for (const auto& [name, desc] : CommandRegistry::getAllCommands()) {
    (void)desc;
    names.emplace_back(name);
  }
  if (std::ranges::find(names, std::string(kInternalToolName)) == names.end()) {
    names.emplace_back(kInternalToolName);
  }
  std::ranges::sort(names);
  names.erase(std::ranges::unique(names).begin(), names.end());
  return names;
}

auto legacy_link_names() -> std::vector<std::string> {
  // Removed built-in commands that may still exist as hardlinks in old
  // installs. 'link' was removed because its hardlink shadows the MSVC
  // toolchain linker link.exe on PATH.
  return {"jq", "link"};
}

auto is_legacy_link_name(std::string_view name) -> bool {
  std::string wanted = exe_suffix(name);
  wanted = lower_ascii(wanted);
  for (const auto& legacy : legacy_link_names()) {
    if (wanted == lower_ascii(exe_suffix(legacy))) return true;
  }
  return false;
}

auto cleanup_link_names() -> std::vector<std::string> {
  auto names = command_names();
  for (const auto& legacy : legacy_link_names()) names.emplace_back(legacy);
  std::ranges::sort(names);
  names.erase(std::ranges::unique(names).begin(), names.end());
  return names;
}

#ifndef FILE_DISPOSITION_DELETE
#define FILE_DISPOSITION_DELETE 0x00000001
#define FILE_DISPOSITION_POSIX_SEMANTICS 0x00000002
#define FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE 0x00000010
#endif

auto delete_file_name(const fs::path& target) -> bool {
  // POSIX-style unlink: removes this directory entry even while the file
  // stays alive elsewhere. Every command link in usr\bin is a hardlink to
  // the same inode as the running wpm/winuxcmd image, so plain DeleteFileW
  // fails with Access denied — the classic self-lock of link-style
  // packaging. FILE_DISPOSITION_POSIX_SEMANTICS removes only this name.
  HANDLE h = CreateFileW(target.wstring().c_str(), DELETE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                         nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    FILE_DISPOSITION_INFO_EX info{};
    info.Flags = FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS |
                 FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE;
    if (SetFileInformationByHandle(h, FileDispositionInfoEx, &info,
                                   sizeof(info))) {
      CloseHandle(h);
      return true;
    }
    CloseHandle(h);
  }
  return DeleteFileW(target.wstring().c_str()) != 0;
}

auto remove_link_if_safe(const fs::path& source, const fs::path& target,
                         const fs::path& current, bool force, bool dry_run)
    -> bool {
  std::error_code ec;
  if (!fs::exists(target, ec)) return true;
  if (same_file(source, target)) return true;
  if (same_path_name(current, target)) {
    safePrintLn(wpm_text("command.wpm.status.keep_running",
                         "wpm: keeping running executable: {}",
                         target.string()));
    return true;
  }
  if (dry_run) return true;
  if (fs::is_directory(target, ec)) {
    safeErrorPrintLn(wpm_text("command.wpm.error.refuse_directory",
                              "wpm: refusing to replace directory: {}",
                              target.string()));
    return false;
  }
  if (!delete_file_name(target)) {
    safeErrorPrintLn(wpm_text("command.wpm.error.remove",
                              "wpm: failed to remove '{}': {}", target.string(),
                              win32_error_text(GetLastError())));
    return false;
  }
  return true;
}

auto remove_stale_legacy_links(const fs::path& root, const fs::path& source,
                               const fs::path& current, bool dry_run,
                               bool verbose) -> std::pair<int, int> {
  const auto current_names = command_names();
  int removed = 0;
  int failed = 0;

  for (const auto& name : legacy_link_names()) {
    if (std::ranges::find(current_names, name) != current_names.end()) continue;
    fs::path target = root / exe_suffix(name);
    if (same_path_name(current, target)) continue;
    std::error_code ec;
    if (!fs::exists(target, ec)) continue;
    if (!same_file(source, target)) continue;

    if (dry_run) {
      safePrintLn(wpm_text("command.wpm.status.remove_legacy",
                           "remove legacy link {}", target.string()));
      ++removed;
      continue;
    }
    if (delete_file_name(target)) {
      ++removed;
      if (verbose)
        safePrintLn(wpm_text("command.wpm.status.removed_legacy",
                             "removed legacy link {}", target.string()));
    } else {
      safeErrorPrintLn(wpm_text("command.wpm.error.remove_legacy",
                                "wpm: failed to remove legacy link '{}': {}",
                                target.string(),
                                win32_error_text(GetLastError())));
      ++failed;
    }
  }

  return {removed, failed};
}

auto materialize_command_link(const fs::path& source, const fs::path& target)
    -> std::error_code {
  if (CreateHardLinkW(target.wstring().c_str(), source.wstring().c_str(),
                      nullptr)) {
    return {};
  }

  if (CreateSymbolicLinkW(target.wstring().c_str(), source.wstring().c_str(),
                          SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    return {};
  }

  std::error_code ec;
  fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
  return ec;
}

/// Defined with the package-upgrade path below; the command-link rebuild needs
/// it too, to park a stale name it cannot delete.
auto move_aside(const fs::path& target, fs::path* parked_out) -> bool;

/// Defined with the install-receipt helpers below; the command-link rebuild
/// walks the receipts to find shim packages the map does not know yet.
auto receipts_dir(const fs::path& root) -> fs::path;

/// Commands of installed `layout: shim` packages that the map does not know
/// yet. An installation that received the forwarder through a self-update has
/// receipts and payloads but no `.wpm/shims.map`, and its shims are still
/// links of winuxcmd.exe; these entries are what that migration records
/// (unixwin/WinuxCmd#1207).
auto missing_shim_map_entries(
    const fs::path& root,
    const std::vector<std::pair<std::string, std::string>>& entries)
    -> std::vector<std::pair<std::string, std::string>> {
  std::vector<std::pair<std::string, std::string>> missing;
  // First pass over the receipts alone: an installation without shim packages
  // pays only a directory listing, never the index parse.
  std::vector<std::string> shim_packages;
  std::error_code ec;
  for (const auto& item :
       fs::directory_iterator(receipts_dir(root),
                              fs::directory_options::skip_permission_denied,
                              ec)) {
    if (ec) break;
    std::error_code file_ec;
    if (!item.is_regular_file(file_ec) || file_ec) continue;
    if (item.path().extension() != ".json") continue;
    auto text = read_text(item.path());
    if (!text) continue;
    auto receipt = parse_json_text(*text);
    if (!receipt || !receipt->is_object()) continue;
    if (receipt->value("layout", "") != "shim") continue;
    const std::string package = receipt->value("name", "");
    if (!package.empty()) shim_packages.push_back(package);
  }
  if (shim_packages.empty()) return missing;

  const auto index = load_index(root);
  for (const auto& package : shim_packages) {
    const auto pkg = find_package(index, package);
    if (!pkg || !(*pkg).contains("commands") ||
        !(*pkg)["commands"].is_array())
      continue;
    for (const auto& command_json : (*pkg)["commands"]) {
      if (!command_json.is_string()) continue;
      const std::string command = command_json.get<std::string>();
      const bool known =
          std::ranges::any_of(entries, [&](const auto& entry) {
            return entry.first == command;
          }) || std::ranges::any_of(missing, [&](const auto& entry) {
            return entry.first == command;
          });
      if (known) continue;
      const fs::path payload =
          package_payload_dir(root, package) / exe_suffix(command);
      std::error_code payload_ec;
      if (!fs::is_regular_file(payload, payload_ec)) continue;
      const fs::path relative = fs::relative(payload, root, ec);
      if (ec) continue;
      missing.emplace_back(command, relative.string());
    }
  }
  return missing;
}

auto rebuild_links(const fs::path& root, bool force, bool dry_run, bool verbose)
    -> int {
  const fs::path canonical_source = canonical_winuxcmd_path(root);
  const fs::path legacy_source = root / "winuxcmd.exe";
  const fs::path source = fs::exists(canonical_source) ? canonical_source
                          : fs::exists(legacy_source)  ? legacy_source
                                                       : current_exe_path();
  const fs::path current = current_exe_path();

  if (!fs::exists(source)) {
    safeErrorPrintLn(wpm_text("command.wpm.error.executable_missing",
                              "wpm: winuxcmd.exe not found in root: {}",
                              root.string()));
    return 1;
  }

  int created = 0;
  int unchanged = 0;
  int failed = 0;
  auto [stale_removed, stale_failed] =
      remove_stale_legacy_links(root, source, current, dry_run, verbose);
  failed += stale_failed;

  for (const auto& name : command_names()) {
    fs::path target = canonical_bin_dir(root) / exe_suffix(name);
    if (same_file(source, target)) {
      ++unchanged;
      continue;
    }
    if (!remove_link_if_safe(source, target, current, force, dry_run)) {
      // A stale name that cannot be deleted - a process still holding it, or a
      // name some other tool created - must not leave this command on the
      // previous image. Park it and link the new one over the freed name, the
      // same way a package upgrade replaces a held payload file
      // (unixwin/WinuxCmd#1194); if even the rename is refused, say which name
      // it was instead of leaving a bare count behind.
      fs::path parked;
      if (!dry_run && move_aside(target, &parked)) {
        auto relink_error = materialize_command_link(source, target);
        if (!relink_error) {
          std::error_code parked_ec;
          fs::remove(parked, parked_ec);
          ++created;
          if (verbose)
            safePrintLn("wpm: relinked " + target.string() +
                        " (parked the previous name)");
          continue;
        }
        std::error_code restore_ec;
        fs::rename(parked, target, restore_ec);
      }
      safeErrorPrintLn("wpm: failed to replace the command link '" +
                       target.string() + "'");
      ++failed;
      continue;
    }
    if (dry_run) {
      safePrintLn(wpm_text("command.wpm.status.link", "link {} -> {}",
                           target.string(), source.string()));
      ++created;
      continue;
    }
    auto link_error = materialize_command_link(source, target);
    if (!link_error) {
      ++created;
      if (verbose)
        safePrintLn(wpm_text("command.wpm.status.linked", "linked {}",
                             target.string()));
    } else {
      if (same_file(source, target)) {
        ++unchanged;
        continue;
      }
      safeErrorPrintLn(wpm_text("command.wpm.error.create_link",
                                "wpm: failed to create hard link '{}': {}",
                                target.string(), link_error.message()));
      // Say what the consequence is: this name keeps running the previous
      // image until something relinks it.
      safeErrorPrintLn("wpm: '" + target.string() +
                       "' still points at the previous winuxcmd.exe");
      ++failed;
    }
  }

  // Package shims live in the same directory and point at the forwarder when it
  // is present. Refreshing them here means an update brings the cheaper
  // dispatch to an existing installation without reinstalling every package
  // (unixwin/WinuxCmd#1207), and that a stale one heals with the rest.
  const fs::path forwarder = package_shim_source(root);
  if (!forwarder.empty() && !dry_run) {
    auto entries = read_shim_map(root);
    // An installation that received the forwarder through a self-update has
    // receipts and payloads but no map yet, and its shims are still links of
    // winuxcmd.exe. Record those payloads so the same pass that relinks the
    // builtins migrates the package shims too; activation runs this rebuild at
    // every shell start, so the migration is one shell start away.
    const auto missing = missing_shim_map_entries(root, entries);
    if (!missing.empty()) {
      for (const auto& [command, payload] : missing) {
        if (verbose)
          safePrintLn("wpm: registered the payload for '" + command + "' in " +
                      shim_map_path(root).string());
      }
      entries.insert(entries.end(), missing.begin(), missing.end());
      write_shim_map(root, entries);
    }
    for (const auto& [command, payload] : entries) {
      const fs::path target = canonical_bin_dir(root) / exe_suffix(command);
      std::error_code payload_ec;
      if (!fs::is_regular_file(root / payload, payload_ec)) continue;
      if (same_file(forwarder, target)) {
        ++unchanged;
        continue;
      }
      if (!remove_link_if_safe(forwarder, target, current, force, dry_run)) {
        // Same contract as the builtin links above: a stale name that cannot
        // be deleted is parked and the forwarder linked over the freed name,
        // so one held shim cannot keep its package on the old dispatcher.
        fs::path parked;
        if (move_aside(target, &parked)) {
          auto relink_error = materialize_command_link(forwarder, target);
          if (!relink_error) {
            std::error_code parked_ec;
            fs::remove(parked, parked_ec);
            ++created;
            if (verbose)
              safePrintLn("wpm: relinked " + target.string() +
                          " (parked the previous name)");
            continue;
          }
          std::error_code restore_ec;
          fs::rename(parked, target, restore_ec);
        }
        safeErrorPrintLn("wpm: failed to replace the package shim '" +
                         target.string() + "'");
        ++failed;
        continue;
      }
      auto link_error = materialize_command_link(forwarder, target);
      if (!link_error) {
        ++created;
        if (verbose) safePrintLn("wpm: relinked " + target.string());
      } else {
        safeErrorPrintLn("wpm: failed to link the package shim '" +
                         target.string() + "': " + link_error.message());
        ++failed;
      }
    }
  }

  safePrintLn(
      wpm_text("command.wpm.status.links_summary",
               "wpm: links created={} unchanged={} stale_removed={} failed={}",
               created, unchanged, stale_removed, failed));
  return failed == 0 ? 0 : 1;
}

/// Names in `usr/bin` that should point at this installation's `winuxcmd.exe`
/// but point somewhere else. That is the residue of an update which could not
/// replace one of them - the command then runs an older build forever, and
/// nothing else notices (unixwin/WinuxCmd#1206). Absent names are not stale:
/// the command set is created, never guaranteed complete.
auto stale_command_links(const fs::path& root) -> std::vector<std::string> {
  std::vector<std::string> stale;
  const fs::path canonical_source = canonical_winuxcmd_path(root);
  const fs::path legacy_source = root / "winuxcmd.exe";
  const fs::path source = fs::exists(canonical_source) ? canonical_source
                          : fs::exists(legacy_source)  ? legacy_source
                                                       : current_exe_path();
  std::error_code exists_ec;
  if (!fs::exists(source, exists_ec)) return stale;
  const fs::path current = current_exe_path();
  for (const auto& name : command_names()) {
    const fs::path target = canonical_bin_dir(root) / exe_suffix(name);
    std::error_code target_ec;
    if (!fs::exists(target, target_ec)) continue;
    if (same_file(source, target)) continue;
    if (same_path_name(current, target)) continue;  // the running image
    stale.emplace_back(name);
  }
  return stale;
}

/// `wpm links verify`: report, by name, the commands that do not run the
/// installed image.
auto verify_links(const Options& opts) -> int {
  const auto stale = stale_command_links(opts.root);
  if (stale.empty()) {
    safePrintLn(wpm_text("command.wpm.status.links_verified",
                         "wpm: every command link points at the installed "
                         "winuxcmd.exe"));
    return 0;
  }
  safeErrorPrintLn(wpm_text("command.wpm.error.links_stale",
                            "wpm: {} command link(s) point at a different "
                            "winuxcmd.exe:",
                            stale.size()));
  for (const auto& name : stale)
    safeErrorPrintLn("  " + (canonical_bin_dir(opts.root) / exe_suffix(name)).string());
  safeErrorPrintLn(
      "wpm: 'wpm links rebuild --force' replaces them from the installed image");
  return 1;
}

auto remove_links(const fs::path& root, bool dry_run) -> int {
  fs::path canonical_source = canonical_winuxcmd_path(root);
  fs::path legacy_source = root / "winuxcmd.exe";
  fs::path source = fs::exists(canonical_source) ? canonical_source
                    : fs::exists(legacy_source)  ? legacy_source
                                                 : current_exe_path();
  fs::path current = current_exe_path();
  int removed = 0;
  int failed = 0;

  for (const auto& name : cleanup_link_names()) {
    fs::path target = canonical_bin_dir(root) / exe_suffix(name);
    if (same_path_name(current, target)) continue;
    std::error_code ec;
    if (!fs::exists(target, ec)) continue;
    if (!same_file(source, target)) continue;
    if (dry_run) {
      safePrintLn(
          wpm_text("command.wpm.status.remove", "remove {}", target.string()));
      ++removed;
      continue;
    }
    // Cluster links name the running image; POSIX unlink only (see
    // delete_file_name) — plain DeleteFileW fails with Access denied.
    if (delete_file_name(target)) {
      ++removed;
    } else {
      safeErrorPrintLn(
          wpm_text("command.wpm.error.remove", "wpm: failed to remove '{}': {}",
                   target.string(), win32_error_text(GetLastError())));
      ++failed;
    }
  }

  safePrintLn(wpm_text("command.wpm.status.links_removed",
                       "wpm: links removed={} failed={}", removed, failed));
  return failed == 0 ? 0 : 1;
}

auto allow_progress_bar_output() -> bool {
  auto terminal = get_terminal_info();
  return terminal.is_tty ||
         GetEnvironmentVariableA("WT_SESSION", nullptr, 0) > 0 ||
         GetEnvironmentVariableA("WINUXSH_WPM_PROGRESS", nullptr, 0) > 0;
}

auto human_size(unsigned long long bytes) -> std::string;

auto make_wpm_progress_bar(std::string_view label,
                           std::optional<unsigned long long> total_bytes)
    -> std::unique_ptr<ProgressBar> {
  std::string message(label);
  if (total_bytes && *total_bytes > 0) {
    message += " (" + human_size(*total_bytes) + ")";
  }

  auto progress = std::make_unique<ProgressBar>(100, message, 40);
  progress->set_style(PresetStyles::modern());
  progress->set_foreground_color(Color(56, 189, 248));
  return progress;
}

class UrlmonProgressCallback : public IBindStatusCallback {
 public:
  UrlmonProgressCallback(std::string label, bool allow_progress_bar)
      : label_(std::move(label)), allow_progress_bar_(allow_progress_bar) {}

  auto finish() -> void {
    if (progress_) {
      progress_->finish();
      progress_.reset();
    }
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void** object) override {
    if (!object) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IBindStatusCallback) {
      *object = static_cast<IBindStatusCallback*>(this);
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }

  ULONG STDMETHODCALLTYPE Release() override { return 1; }

  HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return E_NOTIMPL; }

  HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }

  HRESULT STDMETHODCALLTYPE OnProgress(ULONG progress, ULONG progress_max,
                                       ULONG, LPCWSTR) override {
    if (label_.empty()) return S_OK;

    if (allow_progress_bar_ && progress_max > 0) {
      if (!progress_) {
        progress_ = make_wpm_progress_bar(label_, progress_max);
      }
      int percent = static_cast<int>(
          std::min<ULONG>(100, (progress * 100) / progress_max));
      if (percent != last_percent_) {
        progress_->update(percent);
        last_percent_ = percent;
      }
    } else if (!allow_progress_bar_ && !announced_) {
      safePrintLn(label_);
      announced_ = true;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override {
    finish();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD* bind_flags,
                                        BINDINFO* bind_info) override {
    if (bind_flags) *bind_flags = 0;
    if (bind_info) {
      auto size = bind_info->cbSize;
      std::memset(bind_info, 0, sizeof(BINDINFO));
      bind_info->cbSize = size ? size : sizeof(BINDINFO);
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*,
                                            STGMEDIUM*) override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID, IUnknown*) override {
    return S_OK;
  }

 private:
  std::string label_;
  bool allow_progress_bar_;
  bool announced_ = false;
  int last_percent_ = -1;
  std::unique_ptr<ProgressBar> progress_;
};

auto http_get_urlmon(std::wstring_view url,
                     std::string_view progress_label = {}) -> HttpResult {
  HttpResult result;

  std::array<wchar_t, MAX_PATH> temp_dir{};
  DWORD temp_len =
      GetTempPathW(static_cast<DWORD>(temp_dir.size()), temp_dir.data());
  if (temp_len == 0 || temp_len >= temp_dir.size()) {
    result.error = "URLMon fallback failed to get temp path";
    return result;
  }

  std::array<wchar_t, MAX_PATH> temp_file{};
  if (GetTempFileNameW(temp_dir.data(), L"wpm", 0, temp_file.data()) == 0) {
    result.error = "URLMon fallback failed to create temp file";
    return result;
  }

  UrlmonProgressCallback progress(std::string(progress_label),
                                  allow_progress_bar_output());
  auto* callback = progress_label.empty()
                       ? nullptr
                       : static_cast<IBindStatusCallback*>(&progress);
  HRESULT hr = URLDownloadToFileW(nullptr, std::wstring(url).c_str(),
                                  temp_file.data(), 0, callback);
  progress.finish();
  if (FAILED(hr)) {
    DeleteFileW(temp_file.data());
    result.error =
        std::format("URLMon fallback failed: 0x{:08x}",
                    static_cast<unsigned int>(static_cast<ULONG>(hr)));
    return result;
  }

  std::ifstream in{fs::path(temp_file.data()), std::ios::binary};
  if (!in.is_open()) {
    DeleteFileW(temp_file.data());
    result.error = "URLMon fallback failed to read temp file";
    return result;
  }

  std::string payload{std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>()};
  result.data.resize(payload.size());
  std::memcpy(result.data.data(), payload.data(), payload.size());
  result.ok = true;
  result.status = 200;
  DeleteFileW(temp_file.data());
  return result;
}

auto with_urlmon_fallback(HttpResult result, std::wstring_view url,
                          std::string_view progress_label = {}) -> HttpResult {
  if (!progress_label.empty() && !result.error.empty()) {
    // The primary transport's progress bar already rendered to 100%; say why
    // a second bar is starting instead of looking like a duplicate download.
    safePrintLn(wpm_text("command.wpm.status.retry_fallback_transport",
                         "wpm: download failed ({}); retrying via fallback "
                         "transport",
                         result.error));
  }
  auto fallback = http_get_urlmon(url, progress_label);
  if (fallback.ok) return fallback;
  if (result.error.empty()) return fallback;
  if (!fallback.error.empty()) result.error += "; " + fallback.error;
  return result;
}

auto human_size(unsigned long long bytes) -> std::string {
  constexpr std::array<std::string_view, 5> units = {"B", "KiB", "MiB", "GiB",
                                                     "TiB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < units.size()) {
    value /= 1024.0;
    ++unit;
  }
  if (unit == 0) return std::to_string(bytes) + " B";
  return std::format("{:.1f} {}", value, units[unit]);
}

struct CleanupStats {
  unsigned long long bytes = 0;
  size_t files = 0;
  std::error_code ec;
};

auto directory_stats(const fs::path& root) -> CleanupStats {
  CleanupStats stats;
  std::error_code ec;
  if (!fs::exists(root, ec)) return stats;
  for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    stats.bytes += entry.file_size(ec);
    if (!ec) ++stats.files;
  }
  return stats;
}

auto collect_cleanup_files(const fs::path& root)
    -> std::vector<std::pair<fs::path, unsigned long long>> {
  std::vector<std::pair<fs::path, unsigned long long>> files;
  std::error_code ec;
  fs::recursive_directory_iterator it{root, ec};
  fs::recursive_directory_iterator end;
  while (!ec && it != end) {
    std::error_code file_ec;
    if (it->is_regular_file(file_ec) && !file_ec) {
      files.emplace_back(it->path(), it->file_size(file_ec));
    }
    it.increment(ec);
  }
  return files;
}

auto print_cleanup_progress(std::string_view key, std::string_view kind,
                            size_t done, size_t total, unsigned long long bytes)
    -> void {
  const int percent = total == 0 ? 100 : static_cast<int>(done * 100 / total);
  safePrint(std::string("\r") +
            wpm_text(key, "wpm: removing {}... {}% ({}/{} files, {})", kind,
                     percent, done, total, human_size(bytes)));
}

/// The name a replaced file is parked under until the install commits;
/// leftovers of an interrupted run are swept by `wpm clean`.
constexpr std::string_view kReplacedMarker = ".wpm-old-";

/// Parked names a previous install had to leave behind (their holder has
/// usually exited by now).
auto sweep_replaced_leftovers(const fs::path& root, bool dry_run, bool verbose)
    -> std::pair<size_t, uintmax_t> {
  size_t files = 0;
  uintmax_t bytes = 0;
  for (const char* sub : {"opt", "usr/bin", "bin"}) {
    const fs::path base = root / sub;
    std::error_code exists_ec;
    if (!fs::exists(base, exists_ec)) continue;
    std::error_code walk_ec;
    for (const auto& entry : fs::recursive_directory_iterator(
             base, fs::directory_options::skip_permission_denied, walk_ec)) {
      if (walk_ec) break;
      std::error_code type_ec;
      if (!entry.is_regular_file(type_ec)) continue;
      if (entry.path().filename().string().find(kReplacedMarker) ==
          std::string::npos)
        continue;
      std::error_code size_ec;
      const uintmax_t size = fs::file_size(entry.path(), size_ec);
      if (dry_run) {
        if (verbose) safePrintLn("wpm: would remove " + entry.path().string());
        ++files;
        if (!size_ec) bytes += size;
        continue;
      }
      std::error_code remove_ec;
      fs::remove(entry.path(), remove_ec);
      if (remove_ec) {
        if (verbose)
          safeErrorPrintLn("wpm: could not remove " + entry.path().string() +
                           ": " + remove_ec.message());
        continue;
      }
      ++files;
      if (!size_ec) bytes += size;
    }
  }
  return {files, bytes};
}

auto remove_tree(const fs::path& target, std::string_view kind, bool verbose,
                 bool show_progress = true,
                 std::string_view progress_key =
                     "command.wpm.status.clean_progress") -> CleanupStats {
  CleanupStats removed;
  const auto files = collect_cleanup_files(target);
  const size_t total = files.size();
  size_t done = 0;
  bool progress_shown = false;
  for (const auto& [path, size] : files) {
    if (verbose) {
      safePrintLn(wpm_text("command.wpm.status.clean_file", "wpm: - {}",
                           path.string()));
    }
    std::error_code ec;
    fs::remove(path, ec);
    if (!ec) {
      removed.bytes += size;
      ++removed.files;
    }
    ++done;
    if (!verbose && show_progress && (done % 16 == 0 || done == total)) {
      print_cleanup_progress(progress_key, kind, done, total, removed.bytes);
      progress_shown = true;
    }
  }
  std::error_code ec;
  fs::remove_all(target, ec);
  if (ec) removed.ec = ec;
  if (progress_shown) {
    safePrint(std::string("\r") + std::string(72, ' ') + "\r");
  }
  return removed;
}

auto clean_state_directory(const fs::path& root, std::string_view kind,
                           bool dry_run, bool verbose) -> int {
  const fs::path target = state_dir(root) / std::string(kind);
  const auto stats = directory_stats(target);
  std::error_code ec;
  if (!fs::exists(target, ec)) {
    safePrintLn(wpm_text("command.wpm.status.clean_empty",
                         "wpm: {} is already clean", target.string()));
    return 0;
  }
  if (dry_run) {
    safePrintLn(wpm_text("command.wpm.status.clean_dry_run",
                         "wpm: would remove {} ({} in {} files)",
                         target.string(), human_size(stats.bytes),
                         stats.files));
    return 0;
  }
  const auto started = std::chrono::steady_clock::now();
  const auto removed = remove_tree(target, kind, verbose);
  const std::chrono::duration<double> elapsed =
      std::chrono::steady_clock::now() - started;
  if (removed.ec) {
    safeErrorPrintLn(wpm_text("command.wpm.error.clean",
                              "wpm: failed to clean '{}': {}", target.string(),
                              removed.ec.message()));
    return 1;
  }
  safePrintLn(wpm_text("command.wpm.status.clean_summary",
                       "wpm: removed {} ({} in {} files) in {:.1f}s",
                       target.string(), human_size(removed.bytes),
                       removed.files, elapsed.count()));
  return 0;
}

/// Remove the parked names installs had to leave behind (their holder has
/// usually exited by the time anyone runs this).
auto clean_replaced_leftovers(const Options& opts) -> int {
  const auto [files, bytes] =
      sweep_replaced_leftovers(opts.root, opts.dry_run, opts.verbose);
  if (files == 0) return 0;
  if (opts.dry_run) {
    safePrintLn(wpm_text("command.wpm.status.clean_leftovers_dry_run",
                         "wpm: would remove {} parked file(s) ({})", files,
                         human_size(bytes)));
    return 0;
  }
  safePrintLn(wpm_text("command.wpm.status.clean_leftovers",
                       "wpm: removed {} parked file(s) ({})", files,
                       human_size(bytes)));
  return 0;
}

auto clean_state(const Options& opts, std::string_view selection) -> int {
  std::string kind(selection);
  if (kind.empty() || kind == "all") {
    int result =
        clean_state_directory(opts.root, "cache", opts.dry_run, opts.verbose);
    if (result != 0) return result;
    result = clean_state_directory(opts.root, "staging", opts.dry_run,
                                   opts.verbose);
    if (result != 0) return result;
    // Rollback copies are the third thing that grows without bound, and
    // `clean all` is what a user types when reclaiming space.
    result =
        clean_state_directory(opts.root, "backup", opts.dry_run, opts.verbose);
    if (result != 0) return result;
    return clean_replaced_leftovers(opts);
  }
  if (kind == "cache")
    return clean_state_directory(opts.root, kind, opts.dry_run, opts.verbose);
  if (kind == "staging") {
    int result =
        clean_state_directory(opts.root, kind, opts.dry_run, opts.verbose);
    if (result != 0) return result;
    return clean_replaced_leftovers(opts);
  }
  if (kind == "backup")
    return clean_state_directory(opts.root, kind, opts.dry_run, opts.verbose);
  safeErrorPrintLn(
      winux::i18n::translate("command.wpm.error.usage.clean",
                             "wpm: usage: wpm clean [cache|staging|backup|all]"));
  return 1;
}

auto env_var_present(const char* name) -> bool {
  return GetEnvironmentVariableA(name, nullptr, 0) > 0;
}

auto env_var_value(const char* name) -> std::optional<std::string> {
  DWORD size = GetEnvironmentVariableA(name, nullptr, 0);
  if (size == 0) return std::nullopt;
  std::string value(size, '\0');
  DWORD written = GetEnvironmentVariableA(name, value.data(), size);
  if (written == 0 || written >= size) return std::nullopt;
  value.resize(written);
  return value;
}

auto trim_ascii(std::string value) -> std::string {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!value.empty() &&
         is_space(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

auto normalize_proxy_server(std::string value) -> std::optional<std::wstring> {
  value = trim_ascii(std::move(value));
  if (value.empty()) return std::nullopt;

  auto lower = lower_ascii(value);
  if (lower.find("http=") != std::string::npos ||
      lower.find("https=") != std::string::npos) {
    return utf8_to_wstring(value);
  }

  auto scheme = lower.find("://");
  if (scheme != std::string::npos) {
    auto scheme_name = lower.substr(0, scheme);
    if (scheme_name != "http" && scheme_name != "https") return std::nullopt;
    value.erase(0, scheme + 3);
  }

  if (auto at = value.find('@'); at != std::string::npos) {
    value.erase(0, at + 1);
  }
  if (auto slash = value.find_first_of("/?#"); slash != std::string::npos) {
    value.resize(slash);
  }

  value = trim_ascii(std::move(value));
  if (value.empty()) return std::nullopt;
  return utf8_to_wstring(value);
}

auto proxy_from_environment(bool https) -> std::optional<std::wstring> {
  std::array<std::string_view, 8> names =
      https
          ? std::array<std::string_view, 8>{"WPM_HTTPS_PROXY", "WPM_HTTP_PROXY",
                                            "HTTPS_PROXY",     "https_proxy",
                                            "ALL_PROXY",       "all_proxy",
                                            "HTTP_PROXY",      "http_proxy"}
          : std::array<std::string_view, 8>{"WPM_HTTP_PROXY", "WPM_HTTPS_PROXY",
                                            "HTTP_PROXY",     "http_proxy",
                                            "ALL_PROXY",      "all_proxy",
                                            "HTTPS_PROXY",    "https_proxy"};
  for (auto name : names) {
    std::string key(name);
    if (auto value = env_var_value(key.c_str())) {
      if (auto proxy = normalize_proxy_server(*value)) return proxy;
    }
  }
  return std::nullopt;
}

auto user_forced_proxy(const Options& opts) -> std::optional<std::wstring> {
  if (opts.proxy.empty()) return std::nullopt;
  return normalize_proxy_server(opts.proxy);
}

// Builds a WinHTTP proxy bypass list from WPM_NO_PROXY/NO_PROXY/no_proxy.
// Entries match by host suffix; a bare hostname also matches its subdomains.
// A single "*" bypasses every host, which callers treat as "never proxy".
auto proxy_bypass_from_environment() -> std::optional<std::wstring> {
  std::optional<std::string> raw;
  for (const char* name :
       {"WPM_NO_PROXY", "wpm_no_proxy", "NO_PROXY", "no_proxy"}) {
    if (auto value = env_var_value(name); value && !value->empty()) {
      raw = std::move(*value);
      break;
    }
  }
  if (!raw) return std::nullopt;

  std::wstring bypass;
  size_t start = 0;
  while (start <= raw->size()) {
    size_t end = raw->find(',', start);
    if (end == std::string::npos) end = raw->size();
    std::string entry = trim_ascii(raw->substr(start, end - start));
    start = end + 1;

    if (entry.empty()) continue;
    if (entry == "*") return std::wstring(L"*");
    if (lower_ascii(entry) == "<local>") {
      if (!bypass.empty()) bypass += L';';
      bypass += L"<local>";
      continue;
    }
    if (lower_ascii(entry).starts_with("http://") ||
        lower_ascii(entry).starts_with("https://")) {
      entry.erase(0, entry.find("://") + 3);
    }
    if (auto at = entry.find('/'); at != std::string::npos) {
      entry.resize(at);
    }
    entry = trim_ascii(std::move(entry));
    if (entry.empty()) continue;

    if (!bypass.empty()) bypass += L';';
    std::wstring wide = utf8_to_wstring(entry);
    bypass += wide;
    if (!entry.starts_with('.')) {
      bypass += L';';
      bypass += L'.';
      bypass += wide;
    }
  }
  if (bypass.empty()) return std::nullopt;
  return bypass;
}

void free_proxy_info(WINHTTP_PROXY_INFO& info) {
  if (info.lpszProxy) GlobalFree(info.lpszProxy);
  if (info.lpszProxyBypass) GlobalFree(info.lpszProxyBypass);
  info.lpszProxy = nullptr;
  info.lpszProxyBypass = nullptr;
}

void free_ie_proxy_config(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG& config) {
  if (config.lpszAutoConfigUrl) GlobalFree(config.lpszAutoConfigUrl);
  if (config.lpszProxy) GlobalFree(config.lpszProxy);
  if (config.lpszProxyBypass) GlobalFree(config.lpszProxyBypass);
  config.lpszAutoConfigUrl = nullptr;
  config.lpszProxy = nullptr;
  config.lpszProxyBypass = nullptr;
}

auto set_request_proxy(HINTERNET request, const std::wstring& proxy,
                       LPCWSTR bypass = WINHTTP_NO_PROXY_BYPASS) -> bool {
  WINHTTP_PROXY_INFO info{};
  info.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
  info.lpszProxy = const_cast<LPWSTR>(proxy.c_str());
  info.lpszProxyBypass = const_cast<LPWSTR>(bypass);
  return WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &info, sizeof(info)) !=
         0;
}

auto apply_user_proxy(HINTERNET session, HINTERNET request,
                      std::wstring_view url, bool https) -> void {
  if (auto proxy = proxy_from_environment(https)) {
    set_request_proxy(request, *proxy);
    return;
  }

  WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie_config{};
  if (!WinHttpGetIEProxyConfigForCurrentUser(&ie_config)) return;

  if (ie_config.lpszProxy && *ie_config.lpszProxy) {
    std::wstring proxy(ie_config.lpszProxy);
    LPCWSTR bypass = ie_config.lpszProxyBypass ? ie_config.lpszProxyBypass
                                               : WINHTTP_NO_PROXY_BYPASS;
    set_request_proxy(request, proxy, bypass);
    free_ie_proxy_config(ie_config);
    return;
  }

  if (ie_config.fAutoDetect || ie_config.lpszAutoConfigUrl) {
    WINHTTP_AUTOPROXY_OPTIONS options{};
    if (ie_config.lpszAutoConfigUrl && *ie_config.lpszAutoConfigUrl) {
      options.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
      options.lpszAutoConfigUrl = ie_config.lpszAutoConfigUrl;
    } else {
      options.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
      options.dwAutoDetectFlags =
          WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
    }
    options.fAutoLogonIfChallenged = TRUE;

    WINHTTP_PROXY_INFO auto_proxy{};
    std::wstring url_copy(url);
    if (WinHttpGetProxyForUrl(session, url_copy.c_str(), &options,
                              &auto_proxy)) {
      WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &auto_proxy,
                       sizeof(auto_proxy));
    }
    free_proxy_info(auto_proxy);
  }

  free_ie_proxy_config(ie_config);
}

auto loopback_proxy_candidates() -> std::vector<std::wstring> {
  return {L"127.0.0.1:7897", L"127.0.0.1:7890", L"127.0.0.1:7891",
          L"127.0.0.1:7892", L"127.0.0.1:7893", L"127.0.0.1:10808",
          L"127.0.0.1:10809"};
}

// Loopback proxy probing is opt-in (issue #1177): any unprivileged local
// process can listen on the well-known Clash/V2Ray ports above and serve
// both the index and artifacts, self-consistently passing the index's own
// sha256 checks. Enable only via config `allow_loopback_fallback: true` —
// users with a real proxy should pass --proxy or set the proxy environment
// variables instead, both of which are explicit and trusted.
auto loopback_fallback_allowed(const fs::path& root) -> bool {
  auto config = load_config(root);
  return config.value("allow_loopback_fallback", false);
}

auto response_header(HINTERNET request, DWORD query)
    -> std::optional<std::wstring> {
  DWORD size = 0;
  SetLastError(ERROR_SUCCESS);
  WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
                      WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
    return std::nullopt;
  }

  std::wstring value(size / sizeof(wchar_t), L'\0');
  if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
                           value.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
    return std::nullopt;
  }
  value.resize(size / sizeof(wchar_t));
  while (!value.empty() && value.back() == L'\0') value.pop_back();
  return value;
}

auto redirect_url(std::wstring_view location, bool https,
                  std::wstring_view host, INTERNET_PORT port) -> std::string {
  auto location_utf8 = wstring_to_utf8(location);
  if (starts_with_ci(location_utf8, "http://") ||
      starts_with_ci(location_utf8, "https://")) {
    return location_utf8;
  }

  std::wstring absolute = https ? L"https:" : L"http:";
  if (location.starts_with(L"//")) {
    absolute += std::wstring(location);
  } else {
    absolute += L"//";
    absolute += host;
    bool default_port = (https && port == INTERNET_DEFAULT_HTTPS_PORT) ||
                        (!https && port == INTERNET_DEFAULT_HTTP_PORT);
    if (!default_port) absolute += L":" + std::to_wstring(port);
    if (!location.empty() && location.front() != L'/') absolute += L"/";
    absolute += std::wstring(location);
  }
  return wstring_to_utf8(absolute);
}

// Every caller states allow_loopback_proxy_fallback explicitly: the loopback
// proxy probe is opt-in (issue #1177) and a silent default-on here is how the
// hazard of #1177 existed in the first place.
auto http_get(std::string_view url, std::string_view progress_label,
              int redirects_remaining, std::optional<std::wstring> forced_proxy,
              bool allow_loopback_proxy_fallback) -> HttpResult {
  HttpResult result;

  if (starts_with_ci(url, "file://")) {
    std::string raw(url.substr(7));
    if (raw.size() >= 3 && raw[0] == '/' && raw[2] == ':')
      raw.erase(raw.begin());
    std::ifstream in{fs::path(raw), std::ios::binary};
    if (!in.is_open()) {
      result.error = "failed to open local file source";
      return result;
    }
    std::string payload{std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>()};
    result.data.resize(payload.size());
    std::memcpy(result.data.data(), payload.data(), payload.size());
    result.ok = true;
    result.status = 200;
    return result;
  }

  std::wstring wurl = utf8_to_wstring(std::string(url));
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
    result.error = "failed to parse URL";
    return result;
  }

  std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path_part;
  if (parts.lpszUrlPath) {
    path_part.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  }
  if (parts.lpszExtraInfo) {
    path_part.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  }
  if (path_part.empty()) path_part = L"/";

  bool https = parts.nScheme == INTERNET_SCHEME_HTTPS;
  auto finish_failed = [&](HttpResult failed) -> HttpResult {
    if (!forced_proxy) {
      failed = with_urlmon_fallback(std::move(failed), wurl, progress_label);
    }
    if (failed.ok || forced_proxy || !allow_loopback_proxy_fallback) {
      return failed;
    }

    auto original_error = failed.error;
    for (const auto& proxy : loopback_proxy_candidates()) {
      auto proxied =
          http_get(url, progress_label, redirects_remaining, proxy, false);
      if (proxied.ok) return proxied;
      if (!proxied.error.empty()) {
        failed.error = original_error + "; loopback proxy " +
                       wstring_to_utf8(proxy) + " failed: " + proxied.error;
      }
    }
    return failed;
  };

  HINTERNET session =
      WinHttpOpen(L"WinuxCmd-WPM/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    result.error = "failed to open WinHTTP session";
    return finish_failed(std::move(result));
  }

  HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
  if (!connect) {
    result.error = "failed to connect";
    WinHttpCloseHandle(session);
    return finish_failed(std::move(result));
  }

  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(connect, L"GET", path_part.c_str(),
                                         nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    result.error = "failed to create request";
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return finish_failed(std::move(result));
  }

  if (forced_proxy) {
    auto bypass = proxy_bypass_from_environment();
    if (!(bypass && *bypass == L"*")) {
      set_request_proxy(request, *forced_proxy,
                        bypass ? bypass->c_str() : WINHTTP_NO_PROXY_BYPASS);
    }
  } else {
    apply_user_proxy(session, request, wurl, https);
  }

  DWORD timeout_ms = 12000;
  WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout_ms,
                   sizeof(timeout_ms));
  WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout_ms,
                   sizeof(timeout_ms));
  WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, &timeout_ms,
                   sizeof(timeout_ms));
  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                   sizeof(redirect_policy));

  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    result.error = "request failed: " + win32_error_text(GetLastError());
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return finish_failed(std::move(result));
  }

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  WinHttpQueryHeaders(request,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                      WINHTTP_NO_HEADER_INDEX);
  result.status = status;
  if (status >= 300 && status < 400 && redirects_remaining > 0) {
    auto location = response_header(request, WINHTTP_QUERY_LOCATION);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (location && !location->empty()) {
      return http_get(redirect_url(*location, https, host, parts.nPort),
                      progress_label, redirects_remaining - 1, forced_proxy,
                      allow_loopback_proxy_fallback);
    }
    result.error = "HTTP redirect without Location header";
    return result;
  }
  if (status < 200 || status >= 300) {
    result.error = "HTTP status " + std::to_string(status);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return finish_failed(std::move(result));
  }

  std::optional<unsigned long long> expected_bytes;
  std::array<wchar_t, 64> content_length{};
  DWORD content_length_size =
      static_cast<DWORD>(content_length.size() * sizeof(wchar_t));
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                          WINHTTP_HEADER_NAME_BY_INDEX, content_length.data(),
                          &content_length_size, WINHTTP_NO_HEADER_INDEX)) {
    wchar_t* end = nullptr;
    auto value = std::wcstoull(content_length.data(), &end, 10);
    if (end != content_length.data()) expected_bytes = value;
  }

  auto terminal = get_terminal_info();
  bool allow_progress_bar = terminal.is_tty || env_var_present("WT_SESSION") ||
                            env_var_present("WINUXSH_WPM_PROGRESS");
  bool show_progress = !progress_label.empty() && allow_progress_bar &&
                       expected_bytes && *expected_bytes > 0;
  std::unique_ptr<ProgressBar> progress;
  int last_percent = -1;
  if (show_progress) {
    progress = make_wpm_progress_bar(progress_label, *expected_bytes);
    progress->update(0);
    last_percent = 0;
  } else if (!progress_label.empty()) {
    std::string message(progress_label);
    if (expected_bytes && *expected_bytes > 0) {
      message += " (" + human_size(*expected_bytes) + ")";
    }
    safePrintLn(message);
  }

  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available)) break;
    if (available == 0) break;
    size_t old_size = result.data.size();
    result.data.resize(old_size + available);
    DWORD read = 0;
    if (!WinHttpReadData(request, result.data.data() + old_size, available,
                         &read)) {
      result.error = "read failed: " + win32_error_text(GetLastError());
      result.data.clear();
      break;
    }
    result.data.resize(old_size + read);
    if (progress && expected_bytes) {
      auto percent = static_cast<int>(std::min<unsigned long long>(
          100, (static_cast<unsigned long long>(result.data.size()) * 100) /
                   *expected_bytes));
      if (percent != last_percent) {
        progress->update(percent);
        last_percent = percent;
      }
    }
  }

  if (progress) {
    if (result.error.empty()) {
      progress->finish();
    } else {
      safePrintLn("");
    }
  }

  if (result.error.empty() && expected_bytes &&
      result.data.size() != *expected_bytes) {
    result.error = "incomplete download: expected " +
                   std::to_string(*expected_bytes) + " bytes, got " +
                   std::to_string(result.data.size());
    result.data.clear();
  }

  result.ok = result.error.empty();
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  if (!result.ok) {
    return finish_failed(std::move(result));
  }
  return result;
}

// Lightweight reachability check for index sources. Issues a GET but closes
// after response headers, costing one round trip. Only consulted when the
// source preference is "auto"; the result merely reorders the candidate
// list, so a wrong answer can never skip a working source.
auto probe_url(std::string_view url, DWORD timeout_ms,
               std::optional<std::wstring> forced_proxy) -> bool {
  if (starts_with_ci(url, "file://")) return true;

  std::wstring wurl = utf8_to_wstring(std::string(url));
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) return false;
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path_part;
  if (parts.lpszUrlPath) {
    path_part.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  }
  if (parts.lpszExtraInfo) {
    path_part.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  }
  if (path_part.empty()) path_part = L"/";
  bool https = parts.nScheme == INTERNET_SCHEME_HTTPS;

  HINTERNET session =
      WinHttpOpen(L"WinuxCmd-WPM/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;

  bool ok = false;
  HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
  if (connect) {
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path_part.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (request) {
      if (forced_proxy) {
        auto bypass = proxy_bypass_from_environment();
        if (!(bypass && *bypass == L"*")) {
          set_request_proxy(request, *forced_proxy,
                            bypass ? bypass->c_str() : WINHTTP_NO_PROXY_BYPASS);
        }
      } else {
        apply_user_proxy(session, request, wurl, https);
      }
      WinHttpSetOption(request, WINHTTP_OPTION_RESOLVE_TIMEOUT, &timeout_ms,
                       sizeof(timeout_ms));
      WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout_ms,
                       sizeof(timeout_ms));
      WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, &timeout_ms,
                       sizeof(timeout_ms));
      WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout_ms,
                       sizeof(timeout_ms));
      DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
      WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                       &redirect_policy, sizeof(redirect_policy));
      if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
          WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX);
        ok = status >= 200 && status < 400;
      }
      WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connect);
  }
  WinHttpCloseHandle(session);
  return ok;
}

auto sha256_file(const fs::path& file) -> std::optional<std::string> {
  std::ifstream in(file, std::ios::binary);
  if (!in.is_open()) return std::nullopt;

  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD data_size = 0;
  DWORD hash_size = 0;

  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) !=
      0) {
    return std::nullopt;
  }
  BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                    &data_size, 0);
  BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size),
                    &data_size, 0);

  std::vector<UCHAR> object(object_size);
  std::vector<UCHAR> digest(hash_size);
  if (BCryptCreateHash(alg, &hash, object.data(), object_size, nullptr, 0, 0) !=
      0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return std::nullopt;
  }

  std::array<char, 8192> buffer{};
  while (in.good()) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto got = in.gcount();
    if (got > 0) {
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                     static_cast<ULONG>(got), 0);
    }
  }
  BCryptFinishHash(hash, digest.data(), hash_size, 0);
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);

  static constexpr char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(digest.size() * 2);
  for (auto byte : digest) {
    out.push_back(hex[(byte >> 4) & 0x0f]);
    out.push_back(hex[byte & 0x0f]);
  }
  return out;
}

auto verify_sha256(const fs::path& file, std::string expected) -> bool {
  if (expected.empty()) {
    safeErrorPrintLn("wpm: refusing remote artifact without sha256");
    return false;
  }
  expected = lower_ascii(expected);
  auto actual = sha256_file(file);
  if (!actual) {
    safeErrorPrintLn("wpm: failed to calculate sha256 for " + file.string());
    return false;
  }
  if (*actual != expected) {
    safeErrorPrintLn("wpm: sha256 mismatch for " + file.string());
    safeErrorPrintLn("wpm: expected " + expected);
    safeErrorPrintLn("wpm: actual   " + *actual);
    return false;
  }
  return true;
}

auto cached_artifact_is_valid(const fs::path& file, std::string expected)
    -> bool {
  if (expected.empty()) return false;
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) return false;
  expected = lower_ascii(expected);
  auto actual = sha256_file(file);
  return actual && *actual == expected;
}

auto supported_artifact_type(std::string_view type) -> bool {
  return type == "exe" || type == "zip" || type == "tar.gz" || type == "tgz" ||
         type == "tar.xz";
}

auto archive_artifact_type(std::string_view type) -> bool {
  return type == "zip" || type == "tar.gz" || type == "tgz" || type == "tar.xz";
}

auto artifact_cache_extension(std::string_view type) -> std::string {
  if (type == "tgz") return "tar.gz";
  return std::string(type);
}

// Keep cache file names portable across filesystems; index metadata
// (versions, names) may in principle contain separators or spaces.
auto sanitize_cache_key_component(std::string_view value) -> std::string {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                      c == '_';
    out.push_back(safe ? c : '_');
  }
  return out;
}

// Cache entries are keyed by package, version, and a short artifact hash so a
// stale entry can never satisfy a newer index revision (issue #1154).
auto artifact_cache_filename(std::string_view package, std::string_view version,
                             std::string_view sha256, std::string_view type)
    -> std::string {
  std::string name = sanitize_cache_key_component(package);
  std::string safe_version = sanitize_cache_key_component(version);
  if (!safe_version.empty()) name += "-" + safe_version;
  std::string sha8 = lower_ascii(std::string(sha256)).substr(0, 8);
  if (sha8.size() == 8) name += "-" + sha8;
  return name + "." + artifact_cache_extension(type);
}

auto artifact_size_bytes(const nlohmann::json& artifact)
    -> std::optional<unsigned long long> {
  for (const auto* key : {"size", "size_bytes"}) {
    if (!artifact.contains(key)) continue;
    const auto& value = artifact[key];
    if (value.is_number_unsigned()) {
      return value.get<unsigned long long>();
    }
    if (value.is_number_integer()) {
      auto signed_value = value.get<long long>();
      if (signed_value >= 0)
        return static_cast<unsigned long long>(signed_value);
    }
    if (value.is_string()) {
      try {
        return std::stoull(value.get<std::string>());
      } catch (...) {
      }
    }
  }
  return std::nullopt;
}

auto run_process(const std::wstring& command, const fs::path& cwd = {}) -> int {
  std::wstring mutable_cmd = command;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(
      nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
      nullptr, cwd.empty() ? nullptr : cwd.wstring().c_str(), &si, &pi);
  if (!ok) return static_cast<int>(GetLastError());
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return static_cast<int>(exit_code);
}

auto quote(const fs::path& p) -> std::wstring {
  std::wstring s = p.wstring();
  std::wstring out = L"\"";
  for (wchar_t ch : s) {
    if (ch == L'"') out.push_back(L'\\');
    out.push_back(ch);
  }
  out.push_back(L'"');
  return out;
}

auto extract_archive(const fs::path& archive, const fs::path& dest,
                     std::string_view type, bool json) -> bool {
  std::error_code ec;
  fs::create_directories(dest, ec);
  // --json keeps stdout pure JSON; progress goes to stderr (#1156).
  auto progress = [json](std::string_view line) {
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
  };
  progress(wpm_text("command.wpm.status.extracting", "wpm: extracting {}",
                    archive.filename().string()));
  std::wstring command =
      L"tar.exe -xf " + quote(archive) + L" -C " + quote(dest);
  int code = run_process(command);
  if (code != 0) {
    safeErrorPrintLn(wpm_text("command.wpm.error.extract_failed",
                              "wpm: {} extraction failed; Windows tar.exe "
                              "returned {}",
                              type, code));
    return false;
  }
  return true;
}

auto trim_trailing_separators(std::string value) -> std::string {
  while (!value.empty() && (value.back() == '/' || value.back() == '\\')) {
    value.pop_back();
  }
  return value;
}

auto mapping_is_directory(const nlohmann::json& mapping) -> bool {
  std::string kind = lower_ascii(mapping.value("kind", ""));
  if (kind == "dir" || kind == "directory") return true;
  std::string from = mapping.value("from", "");
  if (!from.empty() && (from.back() == '/' || from.back() == '\\')) return true;
  if (mapping.contains("to") && mapping["to"].is_string()) {
    std::string to = mapping["to"].get<std::string>();
    if (!to.empty() && (to.back() == '/' || to.back() == '\\')) return true;
  }
  return false;
}

auto mapping_default_to(const std::string& from, bool directory)
    -> std::string {
  std::string clean = directory ? trim_trailing_separators(from) : from;
  return fs::path(clean).filename().string();
}

// Resolves the install destination for one files-mapping entry, honoring the
// artifact layout: shim payloads land under opt\<pkg>\, flat ones in usr\bin.
auto artifact_destination_for_mapping(const fs::path& root,
                                      const nlohmann::json& artifact,
                                      std::string_view package,
                                      const nlohmann::json& mapping)
    -> std::optional<fs::path> {
  std::string from = mapping.value("from", "");
  bool directory = mapping_is_directory(mapping);
  std::string to = mapping.contains("to") && mapping["to"].is_string()
                       ? mapping["to"].get<std::string>()
                       : mapping_default_to(from, directory);
  if (from.empty() || to.empty()) return std::nullopt;
  if (artifact_layout(artifact) == "shim") {
    return package_payload_dir(root, package) / fs::path(to).lexically_normal();
  }
  return root / normalized_package_target(to, directory);
}

auto find_file_recursive(const fs::path& root, std::string_view filename)
    -> std::optional<fs::path> {
  std::error_code ec;
  if (!fs::exists(root, ec)) return std::nullopt;
  for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    if (lower_ascii(entry.path().filename().string()) ==
        lower_ascii(std::string(filename))) {
      return entry.path();
    }
  }
  return std::nullopt;
}

auto find_directory_recursive(const fs::path& root, std::string_view dirname)
    -> std::optional<fs::path> {
  std::error_code ec;
  if (!fs::exists(root, ec)) return std::nullopt;
  for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_directory(ec)) continue;
    if (lower_ascii(entry.path().filename().string()) ==
        lower_ascii(std::string(dirname))) {
      return entry.path();
    }
  }
  return std::nullopt;
}

auto find_single_regular_file(const fs::path& root) -> std::optional<fs::path> {
  std::error_code ec;
  std::optional<fs::path> only;
  if (!fs::exists(root, ec)) return std::nullopt;
  for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    if (only) return std::nullopt;
    only = entry.path();
  }
  return only;
}

auto find_artifact_path(const fs::path& root, std::string_view from,
                        bool directory, bool allow_single_file_fallback)
    -> std::optional<fs::path> {
  std::string clean = directory ? trim_trailing_separators(std::string(from))
                                : std::string(from);
  fs::path requested = fs::path(clean);
  std::error_code ec;
  auto direct = root / requested;
  if (directory) {
    if (fs::is_directory(direct, ec)) return direct;
    return find_directory_recursive(root, requested.filename().string());
  }
  if (fs::is_regular_file(direct, ec)) return direct;
  if (auto found = find_file_recursive(root, requested.filename().string()))
    return found;
  if (allow_single_file_fallback) return find_single_regular_file(root);
  return std::nullopt;
}

auto materialize_file(const fs::path& source, const fs::path& target,
                      bool force, std::error_code& ec) -> bool {
  ec.clear();
  if (force) fs::remove(target, ec);
  if (ec) return false;
  if (!force && fs::exists(target, ec)) return false;
  if (CreateHardLinkW(target.wstring().c_str(), source.wstring().c_str(),
                      nullptr))
    return true;
  const DWORD link_error = GetLastError();
  ec = std::error_code(static_cast<int>(link_error), std::system_category());
  fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

auto copy_directory_contents(const fs::path& src, const fs::path& dest,
                             bool force) -> bool {
  std::error_code ec;
  if (fs::exists(dest, ec) && !fs::is_directory(dest, ec)) {
    safeErrorPrintLn("wpm: destination exists and is not a directory: " +
                     dest.string());
    return false;
  }
  fs::create_directories(dest, ec);
  if (ec) {
    safeErrorPrintLn("wpm: failed to create directory '" + dest.string() +
                     "': " + ec.message());
    return false;
  }

  for (const auto& entry : fs::recursive_directory_iterator(src, ec)) {
    if (ec) {
      safeErrorPrintLn("wpm: failed to read directory '" + src.string() +
                       "': " + ec.message());
      return false;
    }
    auto relative = fs::relative(entry.path(), src, ec);
    if (ec) {
      safeErrorPrintLn("wpm: failed to map directory entry '" +
                       entry.path().string() + "': " + ec.message());
      return false;
    }
    auto target = dest / relative;
    if (entry.is_directory(ec)) {
      fs::create_directories(target, ec);
      if (ec) {
        safeErrorPrintLn("wpm: failed to create directory '" + target.string() +
                         "': " + ec.message());
        return false;
      }
      continue;
    }
    if (!entry.is_regular_file(ec)) continue;
    fs::create_directories(target.parent_path(), ec);
    if (force) {
      std::error_code exists_ec;
      if (fs::exists(target, exists_ec) && !delete_file_name(target)) {
        safeErrorPrintLn("wpm: failed to replace '" + target.string() + "'");
        return false;
      }
      // POSIX unlink rather than an in-place overwrite: a payload file can be
      // held by a process from the previous version (git's fsmonitor daemon
      // holds libexec/git-core/git.exe while any monitored repository keeps it
      // alive), and an overwrite then fails with a sharing violation and
      // aborts the upgrade half-applied. Unlinking the name leaves the holder
      // on the old inode and lets the new bytes land.
    }
    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing,
                  ec);
    if (ec) {
      safeErrorPrintLn("wpm: failed to copy '" + entry.path().string() +
                       "' to '" + target.string() + "': " + ec.message());
      return false;
    }
  }
  return true;
}

auto artifact_for_current_arch(const nlohmann::json& pkg)
    -> std::optional<nlohmann::json> {
  if (!pkg.contains("artifacts") || !pkg["artifacts"].is_object())
    return std::nullopt;
  std::string key = detect_arch_key();
  if (pkg["artifacts"].contains(key)) return pkg["artifacts"][key];
  return std::nullopt;
}

auto artifact_urls(const nlohmann::json& artifact) -> std::vector<std::string> {
  std::vector<std::string> urls;
  if (!artifact.contains("urls")) return urls;
  if (artifact["urls"].is_array()) {
    for (const auto& item : artifact["urls"]) {
      if (item.is_string()) urls.push_back(item.get<std::string>());
      if (item.is_object() && item.contains("url"))
        urls.push_back(item["url"].get<std::string>());
    }
  }
  return urls;
}

auto join_json_string_array(const nlohmann::json& object, std::string_view key)
    -> std::string {
  if (!object.contains(std::string(key)) ||
      !object[std::string(key)].is_array()) {
    return "";
  }

  std::vector<std::string> values;
  for (const auto& item : object[std::string(key)]) {
    if (item.is_string()) values.push_back(item.get<std::string>());
  }

  std::string out;
  for (const auto& value : values) {
    if (!out.empty()) out += ", ";
    out += value;
  }
  return out;
}

auto artifact_install_state(const nlohmann::json& pkg) -> std::string {
  auto artifact = artifact_for_current_arch(pkg);
  if (!artifact) return "metadata-only (no current-arch artifact)";
  if (!supported_artifact_type(artifact->value("type", "exe")))
    return "metadata-only (unsupported artifact type)";
  if (artifact_urls(*artifact).empty())
    return "metadata-only (no download URLs)";
  if (artifact->value("sha256", "").empty())
    return "metadata-only (missing sha256)";
  if (!artifact->contains("files") || !(*artifact)["files"].is_array() ||
      (*artifact)["files"].empty())
    return "metadata-only (missing files mapping)";
  return "ready";
}

auto package_state_label(const nlohmann::json& pkg) -> std::string {
  if (!package_alias_target(pkg).empty()) return "alias";
  return artifact_install_state(pkg) == "ready" ? "ready" : "index-only";
}

auto contains_ci(std::string_view text, std::string_view needle) -> bool {
  if (needle.empty()) return true;
  return lower_ascii(std::string(text))
             .find(lower_ascii(std::string(needle))) != std::string::npos;
}

auto package_matches_query(const nlohmann::json& pkg, std::string_view query)
    -> bool {
  if (query.empty()) return true;
  if (contains_ci(pkg.value("name", ""), query)) return true;
  if (contains_ci(pkg.value("description", ""), query)) return true;
  if (contains_ci(pkg.value("category", ""), query)) return true;
  if (contains_ci(pkg.value("license", ""), query)) return true;
  return contains_ci(join_json_string_array(pkg, "commands"), query);
}

auto package_matches_category(const nlohmann::json& pkg,
                              std::string_view category) -> bool {
  if (category.empty()) return true;
  return lower_ascii(pkg.value("category", "")) ==
         lower_ascii(std::string(category));
}

auto download_artifact(const fs::path& root, const std::string& package,
                       std::string_view version, const nlohmann::json& artifact,
                       bool verbose,
                       const std::optional<std::wstring>& forced_proxy,
                       bool json) -> std::optional<fs::path> {
  // --json keeps stdout pure JSON; progress goes to stderr (#1156).
  auto progress = [json](std::string_view line) {
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
  };
  auto urls = artifact_urls(artifact);
  if (urls.empty()) {
    safeErrorPrintLn(wpm_text("command.wpm.error.no_urls",
                              "wpm: package '{}' has no URLs in the local "
                              "index",
                              package));
    safeErrorPrintLn(wpm_text("command.wpm.error.run_index_update_hint",
                              "wpm: run 'wpm index update' or choose another "
                              "source"));
    return std::nullopt;
  }

  std::string type = artifact.value("type", "exe");
  std::string expected_sha = artifact.value("sha256", "");
  fs::path out = cache_dir(root) /
                 artifact_cache_filename(package, version, expected_sha, type);
  // The cache hit is accepted only when the cached file still hashes to the
  // index artifact sha256; anything else (corruption, stale entry) falls
  // through to a fresh download below.
  if (cached_artifact_is_valid(out, expected_sha)) {
    if (verbose)
      progress(wpm_text("command.wpm.status.using_cache",
                        "wpm: using cached {}", out.string()));
    return out;
  }
  if (fs::exists(out)) {
    std::error_code ec;
    fs::remove(out, ec);
  }
  for (const auto& url : urls) {
    if (verbose) progress("wpm: downloading " + url);
    // An empty label disables the progress bar entirely: --json output must
    // stay machine-readable even when WT_SESSION would allow bar output.
    std::string progress_label;
    if (!json) {
      progress_label = wpm_text("command.wpm.status.downloading",
                                "wpm: downloading {}", package);
    }
    auto result = http_get(url, progress_label, 5, forced_proxy,
                           loopback_fallback_allowed(root));
    if (!result.ok) {
      safeErrorPrintLn(wpm_text("command.wpm.error.download_failed",
                                "wpm: download failed from {}: {}", url,
                                result.error));
      continue;
    }
    if (!write_bytes(out, result.data)) {
      safeErrorPrintLn(wpm_text("command.wpm.error.write_cache",
                                "wpm: failed to write cache file {}",
                                out.string()));
      continue;
    }
    if (!verify_sha256(out, expected_sha)) {
      std::error_code ec;
      fs::remove(out, ec);
      continue;
    }
    return out;
  }

  return std::nullopt;
}

/// When a file mapping's source also lives inside a directory mapping (the
/// flat layout maps `mingw64/` *and* `mingw64/bin/git.exe`), the authoritative
/// source is the materialized payload, not the extraction directory: linking
/// out of staging left the PATH entry as a second copy on disk and tied it to
/// a transient directory, while the manifest - and every documented upgrade -
/// talks about `opt/<pkg>/...` (unixwin/WinuxCmd#1188).
auto payload_source_for(const fs::path& extracted, const fs::path& root,
                        const nlohmann::json& artifact,
                        std::string_view package, const fs::path& source)
    -> std::optional<fs::path> {
  if (!artifact.contains("files") || !artifact["files"].is_array())
    return std::nullopt;
  const bool single_file_fallback = artifact.value("type", "exe") == "exe";
  for (const auto& mapping : artifact["files"]) {
    if (!mapping_is_directory(mapping)) continue;
    const std::string from = mapping.value("from", "");
    if (from.empty()) continue;
    auto directory_source =
        find_artifact_path(extracted, from, true, single_file_fallback);
    if (!directory_source) continue;
    std::error_code rel_ec;
    const fs::path relative = fs::relative(source, *directory_source, rel_ec);
    if (rel_ec || relative.empty() || relative.native().starts_with(L".."))
      continue;
    auto directory_dest =
        artifact_destination_for_mapping(root, artifact, package, mapping);
    if (!directory_dest) continue;
    std::error_code exists_ec;
    const fs::path candidate = *directory_dest / relative;
    if (fs::exists(candidate, exists_ec)) return candidate;
  }
  return std::nullopt;
}

/// The PATH entry directory the host puts in front of users (`\bin`, not
/// `\usr\bin`: that one carries hundreds of tools and must stay off PATH).
auto entry_dir(const fs::path& root) -> fs::path { return root / "bin"; }

/// Commands the shim/forwarder pair answers with a fixed argv prefix before
/// the payload sees its arguments (unixwin/niubash#467: ezwinports gawk is a
/// text-mode mingw build, and `-v BINMODE=3` is the only runtime lever it
/// honors for binary stdio). Their PATH-entry link must route through a
/// forwarder as well, or `bin\<cmd>` would reach the payload unadapted.
/// Mirrors the adapter table in src/shims/winuxcmd_shim.cpp and
/// opt_forward_argv_prefix in src/Main/main.cpp.
auto opt_forward_adapted(std::string_view command) -> bool {
  const std::string lowered = lower_ascii(std::string(command));
  return lowered == "awk" || lowered == "gawk";
}

/// Where each command of a package resolves inside its payload, paired with
/// the entry link the host PATH needs: `<root>/bin/<cmd>` as a hardlink to the
/// real binary - never a launcher (a link is free, a forwarding process costs
/// ~20 ms per invocation). Declaring the links here means install, update and
/// uninstall keep the PATH set in sync instead of leaving stale binaries
/// behind when the payload moves on (unixwin/niubash#376 rule 5).
auto package_entry_links(const fs::path& root, const nlohmann::json& pkg,
                         const nlohmann::json& artifact)
    -> std::vector<std::pair<fs::path, fs::path>> {
  std::vector<std::pair<fs::path, fs::path>> links;
  if (!pkg.contains("commands") || !pkg["commands"].is_array()) return links;
  const std::string package = pkg.value("name", "");
  if (package.empty()) return links;
  for (const auto& item : pkg["commands"]) {
    if (!item.is_string()) continue;
    const std::string command = item.get<std::string>();
    if (command.empty()) continue;
    fs::path payload;
    if (artifact_layout(artifact) == "shim") {
      payload = package_payload_dir(root, package) / exe_suffix(command);
      // unixwin/niubash#467: commands the forwarders answer with an argv
      // adapter (gawk's `-v BINMODE=3`) must route the PATH-entry link through
      // a forwarder as well - a link to the payload itself would bypass the
      // adapter and leak the payload's raw stdio behaviour. Prefer the small
      // forwarder; an installation that predates it still forwards correctly
      // through winuxcmd.exe's opt-payload dispatch.
      if (opt_forward_adapted(command)) {
        payload = package_shim_source(root);
        if (payload.empty()) payload = canonical_winuxcmd_path(root);
      }
    } else if (artifact.contains("files") && artifact["files"].is_array()) {
      // Flat layouts map the command binary itself; that mapping's destination
      // is the payload copy.
      for (const auto& mapping : artifact["files"]) {
        if (mapping_is_directory(mapping)) continue;
        auto dest =
            artifact_destination_for_mapping(root, artifact, package, mapping);
        if (!dest) continue;
        if (dest->filename() == exe_suffix(command)) {
          payload = *dest;
          break;
        }
      }
    }
    if (payload.empty()) continue;
    links.emplace_back(payload, entry_dir(root) / exe_suffix(command));
  }
  return links;
}

/// Materialize the package's entry links (idempotent: identical content is
/// left alone, a link that points at older bytes is rebuilt).
auto create_package_entry_links(const fs::path& root, const nlohmann::json& pkg,
                                const nlohmann::json& artifact, bool force,
                                bool dry_run, bool json) -> bool {
  auto progress = [json](std::string_view line) {
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
  };
  const fs::path current = current_exe_path();
  for (const auto& [payload, link] : package_entry_links(root, pkg, artifact)) {
    std::error_code ec;
    if (!fs::exists(payload, ec)) continue;
    if (same_path_name(current, link)) continue;  // never touch the running exe
    if (same_file(payload, link)) continue;       // already the right bytes
    if (dry_run) {
      if (json) {
        safeErrorPrintLn("wpm: dry-run: would link " + link.string());
      } else {
        progress("wpm: would link " + link.string());
      }
      continue;
    }
    if (materialize_file(payload, link, force, ec)) {
      progress("wpm: linked " + link.string());
      continue;
    }
    safeErrorPrintLn("wpm: failed to create the entry link '" + link.string() +
                     "': " + ec.message());
    return false;
  }
  return true;
}

/// The main image of every process running from `target` (the usual holder of a
/// payload file, and the one a user can actually close).
auto processes_running_from(const fs::path& target) -> std::vector<std::string> {
  std::vector<std::string> holders;
  const auto wanted = normalized_path_key(target);  // std::wstring key
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return holders;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   entry.th32ProcessID);
      if (process == nullptr) continue;
      WCHAR buffer[MAX_PATH * 2];
      DWORD size = static_cast<DWORD>(std::size(buffer));
      if (QueryFullProcessImageNameW(process, 0, buffer, &size) != 0) {
        const fs::path image(buffer);
        if (normalized_path_key(image) == wanted)
          holders.push_back(image.string());
      }
      CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return holders;
}

/// A destination this artifact has to replace but cannot even rename, with the
/// processes to close.
struct BlockedReplacement {
  fs::path target;
  std::vector<std::string> holders;
};

/// A destination parked out of the way so the new bytes can take its name.
struct StagedReplacement {
  fs::path original;
  fs::path parked;
};

/// Park `target` so its name becomes free. Renaming - not deleting - is what
/// works on a file another process has mapped: measured on a live install,
/// `CreateFileW(DELETE)` *succeeds* against git's fsmonitor daemon holding
/// `libexec/git-core/git.exe`, while `SetFileInformationByHandle(
/// FILE_DISPOSITION_INFO)` and an in-place overwrite both fail with Access
/// denied. A "can I delete this name?" probe therefore lies in exactly the case
/// that matters, and the honest answer is the operation itself: the rename
/// leaves the running process on its old inode and is reversible until the
/// install commits (unixwin/WinuxCmd#1188).
auto move_aside(const fs::path& target, fs::path* parked_out) -> bool {
  if (parked_out == nullptr) return false;
  fs::path parked = target;
  parked += std::string(kReplacedMarker) + std::to_string(GetCurrentProcessId());
  std::error_code leftover_ec;
  fs::remove(parked, leftover_ec);  // from an interrupted install, if any
  std::error_code ec;
  fs::rename(target, parked, ec);
  if (ec) return false;
  *parked_out = std::move(parked);
  return true;
}

/// What one install parked, and what it could not park.
struct ReplacementStaging {
  std::vector<StagedReplacement> parked;
  std::vector<BlockedReplacement> blocked;
};

auto restore_parked(const std::vector<StagedReplacement>& parked) {
  for (const auto& item : parked) {
    std::error_code ec;
    fs::rename(item.parked, item.original, ec);
  }
}

/// Drop the parked copies. Best effort by design: while the old process still
/// holds them the bytes cannot be removed, so the name stays until `wpm clean`
/// (or the next install of the same package) can take it.
auto discard_parked(const ReplacementStaging& staging, bool verbose) -> size_t {
  size_t leftovers = 0;
  for (const auto& item : staging.parked) {
    std::error_code ec;
    fs::remove(item.parked, ec);
    if (ec) {
      ++leftovers;
      if (verbose)
        safeErrorPrintLn("wpm: note: '" + item.parked.string() +
                         "' is still held by a running process; `wpm clean "
                         "staging` removes it later");
    }
  }
  return leftovers;
}

/// Park every destination this artifact will replace, in one pass, before
/// anything is written. A failure anywhere restores what was already parked, so
/// the installation is either fully staged or untouched - never half-updated
/// (the failure that used to leave `opt/niugit` on the new version while the
/// PATH entry still ran the old one).
auto stage_replacements(const fs::path& extracted, const fs::path& root,
                        const nlohmann::json& artifact,
                        std::string_view package, const nlohmann::json& pkg)
    -> ReplacementStaging {
  ReplacementStaging staging;
  const fs::path core = canonical_winuxcmd_path(root);
  const fs::path current = current_exe_path();
  const bool single_file_fallback = artifact.value("type", "exe") == "exe";

  std::vector<fs::path> targets;
  auto add_target = [&](const fs::path& target) {
    std::error_code exists_ec;
    if (!fs::exists(target, exists_ec)) return;
    if (same_file(core, target) || same_path_name(current, target)) return;
    const auto key = normalized_path_key(target);
    for (const auto& existing : targets)
      if (normalized_path_key(existing) == key) return;
    targets.push_back(target);
  };

  if (artifact.contains("files") && artifact["files"].is_array()) {
    for (const auto& mapping : artifact["files"]) {
      const bool directory = mapping_is_directory(mapping);
      auto dest =
          artifact_destination_for_mapping(root, artifact, package, mapping);
      if (!dest) continue;
      if (!directory) {
        add_target(*dest);
        continue;
      }
      const std::string from = mapping.value("from", "");
      auto source =
          find_artifact_path(extracted, from, true, single_file_fallback);
      if (!source) continue;
      std::error_code walk_ec;
      for (const auto& entry : fs::recursive_directory_iterator(
               *source, fs::directory_options::skip_permission_denied,
               walk_ec)) {
        if (walk_ec) break;
        std::error_code type_ec;
        if (!entry.is_regular_file(type_ec)) continue;
        std::error_code rel_ec;
        const fs::path relative = fs::relative(entry.path(), *source, rel_ec);
        if (rel_ec) continue;
        add_target(*dest / relative);
      }
    }
  }
  // The PATH entry links this package owns are replaced too, and a program
  // running from one of them holds it just like a payload file.
  for (const auto& [payload, link] : package_entry_links(root, pkg, artifact)) {
    (void)payload;
    add_target(link);
  }

  for (const auto& target : targets) {
    fs::path parked;
    if (move_aside(target, &parked)) {
      staging.parked.push_back({target, std::move(parked)});
      continue;
    }
    staging.blocked.push_back({target, processes_running_from(target)});
  }

  if (!staging.blocked.empty()) {
    // Nothing has been written yet: put back everything that was parked so the
    // user retries from an unwounded installation.
    restore_parked(staging.parked);
    staging.parked.clear();
  }
  return staging;
}

auto copy_artifact_files(const fs::path& extracted, const fs::path& root,
                         const nlohmann::json& artifact, bool force,
                         bool dry_run, std::string_view package, bool json)
    -> bool {
  // --json keeps stdout pure JSON; progress goes to stderr (#1156).
  auto progress = [json](std::string_view line) {
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
  };
  if (!artifact.contains("files") || !artifact["files"].is_array()) {
    safeErrorPrintLn("wpm: artifact has no files mapping");
    return false;
  }

  for (const auto& mapping : artifact["files"]) {
    std::string from = mapping.value("from", "");
    bool directory = mapping_is_directory(mapping);
    std::string to = mapping.contains("to") && mapping["to"].is_string()
                         ? mapping["to"].get<std::string>()
                         : mapping_default_to(from, directory);
    if (from.empty() || to.empty()) {
      safeErrorPrintLn("wpm: invalid file mapping in artifact");
      return false;
    }

    bool single_file_fallback = artifact.value("type", "exe") == "exe";
    auto src =
        find_artifact_path(extracted, from, directory, single_file_fallback);
    if (!src) {
      safeErrorPrintLn("wpm: extracted path not found: " + from);
      return false;
    }

    auto mapped_dest =
        artifact_destination_for_mapping(root, artifact, package, mapping);
    if (!mapped_dest) {
      safeErrorPrintLn("wpm: invalid file mapping in artifact");
      return false;
    }
    fs::path dest = *mapped_dest;
    // A file mapping whose source also lives inside a directory mapping must
    // be sourced from the materialized payload (payload_source_for): taking it
    // from the extraction directory hardlinked the PATH entry to a transient
    // tree and left a second full copy on disk (unixwin/WinuxCmd#1188).
    fs::path source = *src;
    if (!directory) {
      if (auto payload =
              payload_source_for(extracted, root, artifact, package, *src))
        source = *payload;
    }
    std::error_code ec;
    bool dest_exists = fs::exists(dest, ec);
    bool dest_is_winux_link =
        dest_exists && same_file(canonical_winuxcmd_path(root), dest) &&
        !same_path_name(canonical_winuxcmd_path(root), dest);
    bool dest_is_legacy_link = is_legacy_link_name(dest.filename().string());
    if (directory && dest_exists && !fs::is_directory(dest, ec)) {
      safeErrorPrintLn("wpm: destination exists and is not a directory: " +
                       dest.string());
      return false;
    }
    if (directory && dest_exists && !force) {
      safeErrorPrintLn("wpm: destination exists; use --force: " +
                       dest.string());
      return false;
    }
    if (dest_exists && !dest_is_winux_link && !force && !directory &&
        !same_file(source, dest)) {
      safeErrorPrintLn("wpm: destination exists; use --force: " +
                       dest.string());
      return false;
    }
    if (dest_is_winux_link && !force && !dest_is_legacy_link) {
      safeErrorPrintLn(
          "wpm: destination is a WinuxCmd hardlink; use --force: " +
          dest.string());
      return false;
    }
    if (dry_run) {
      progress(
          std::string(directory ? "would copy directory " : "would copy ") +
          src->string() + " -> " + dest.string());
      continue;
    }
    fs::create_directories(dest.parent_path(), ec);
    if (directory) {
      if (!copy_directory_contents(*src, dest, force)) return false;
      continue;
    }
    if (dest_is_winux_link) {
      // dest is a name of the running wpm/winuxcmd image itself: plain
      // DeleteFileW would self-lock (Access denied). POSIX unlink removes
      // only this name, leaving the image alive (see delete_file_name).
      if (!delete_file_name(dest)) {
        safeErrorPrintLn("wpm: failed to break WinuxCmd hardlink '" +
                         dest.string() +
                         "': " + win32_error_text(GetLastError()));
        return false;
      }
    }
    if (!materialize_file(source, dest, true, ec)) {
      safeErrorPrintLn(wpm_text("command.wpm.error.materialize",
                                "wpm: failed to install '{}' to '{}': {}",
                                source.string(), dest.string(), ec.message()));
      return false;
    }
  }
  return true;
}

auto artifact_destination_paths(const fs::path& root,
                                const nlohmann::json& artifact,
                                std::string_view package)
    -> std::optional<std::vector<fs::path>> {
  if (!artifact.contains("files") || !artifact["files"].is_array()) {
    safeErrorPrintLn("wpm: artifact has no files mapping");
    return std::nullopt;
  }

  std::vector<fs::path> destinations;
  for (const auto& mapping : artifact["files"]) {
    auto dest =
        artifact_destination_for_mapping(root, artifact, package, mapping);
    if (!dest) {
      safeErrorPrintLn("wpm: invalid file mapping in artifact");
      return std::nullopt;
    }
    destinations.push_back(*dest);
  }
  return destinations;
}

// Install receipts record the packages wpm itself placed on disk. Installed
// judgments based on destination existence alone misreport same-named
// commands from other sources (an MSYS-linked gawk assembled into usr\bin,
// for example) as "already installed", skipping the download and sha256
// verification entirely. With receipts, only wpm's own installs count.
auto receipts_dir(const fs::path& root) -> fs::path {
  return root / ".wpm" / "installed";
}

auto receipt_path(const fs::path& root, std::string_view package) -> fs::path {
  return receipts_dir(root) / (std::string(package) + ".json");
}

auto package_has_receipt(const fs::path& root, std::string_view package)
    -> bool {
  std::error_code ec;
  return fs::is_regular_file(receipt_path(root, package), ec);
}

// Read the version an install receipt recorded. Receipts capture what wpm
// actually placed on disk; the local index copy is rewritten by every
// `index update`, so it cannot tell which version is installed (#1156:
// outdated compared the local index against a freshly fetched remote index
// and reported "up to date" forever after the first index update).
auto receipt_version(const nlohmann::json& receipt) -> std::string {
  if (!receipt.is_object()) return "";
  return receipt.value("version", "");
}

// Enumerate receipts as the authoritative record of wpm-installed packages:
// name -> installed version. A receipt only counts while every recorded
// destination still exists (manual deletion means the package is gone).
auto receipt_install_records(const fs::path& root)
    -> std::map<std::string, std::string> {
  std::map<std::string, std::string> installed;
  std::error_code ec;
  fs::directory_iterator it(receipts_dir(root), ec);
  if (ec) return installed;
  for (; it != fs::end(it); it.increment(ec)) {
    if (ec) break;
    std::error_code file_ec;
    if (!it->is_regular_file(file_ec)) continue;
    if (it->path().extension() != ".json") continue;
    auto text = read_text(it->path());
    if (!text) continue;
    auto parsed = parse_json_text(*text);
    if (!parsed) continue;
    const std::string name = parsed->value("name", "");
    if (name.empty() || name == "winuxcmd") continue;
    bool present = true;
    if (parsed->contains("destinations") &&
        (*parsed)["destinations"].is_array())
      for (const auto& dest : (*parsed)["destinations"]) {
        if (!dest.is_string()) continue;
        std::error_code exists_ec;
        if (!fs::exists(fs::path(dest.get<std::string>()), exists_ec) ||
            exists_ec) {
          present = false;
          break;
        }
      }
    if (!present) continue;
    installed.emplace(name, receipt_version(*parsed));
  }
  return installed;
}

auto write_install_receipt(const fs::path& root, const nlohmann::json& pkg,
                           const nlohmann::json& artifact) -> void {
  const std::string name = pkg.value("name", "");
  if (name.empty()) return;
  auto destinations = artifact_destination_paths(root, artifact, name);
  if (!destinations) return;
  nlohmann::json receipt = {{"name", name},
                            {"version", pkg.value("version", "")},
                            {"sha256", artifact.value("sha256", "")},
                            {"layout", artifact_layout(artifact)},
                            {"destinations", nlohmann::json::array()}};
  for (const auto& dest : *destinations) {
    receipt["destinations"].push_back(dest.string());
  }
  // The PATH entry links are managed destinations too, so a later reconcile or
  // an uninstall removes them with the package.
  for (const auto& [payload, link] : package_entry_links(root, pkg, artifact)) {
    (void)payload;
    receipt["destinations"].push_back(link.string());
  }
  std::error_code ec;
  fs::create_directories(receipts_dir(root), ec);
  std::ofstream out(receipt_path(root, name),
                    std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return;
  out << receipt.dump(2) << "\n";
}

auto remove_install_receipt(const fs::path& root, std::string_view package)
    -> void {
  std::error_code ec;
  fs::remove(receipt_path(root, package), ec);
}

// Decides whether an install may proceed without --force. Returns nullopt to
// proceed, 0 for the benign "already installed" stop, 1 for a refusal. The
// human messages are echoed to stderr as usual; `messages` (when non-null)
// collects them for the --json result payload (#1156).
auto preflight_install_destinations(const fs::path& root,
                                    const nlohmann::json& artifact,
                                    std::string_view package, bool force,
                                    bool json,
                                    std::vector<std::string>* messages)
    -> std::optional<int> {
  auto note = [messages](std::string text) {
    if (messages != nullptr) messages->push_back(text);
  };
  auto destinations = artifact_destination_paths(root, artifact, package);
  if (!destinations) return 1;
  if (force) return std::nullopt;

  size_t already_present = 0;
  for (const auto& dest : *destinations) {
    std::error_code ec;
    bool dest_exists = fs::exists(dest, ec);
    if (!dest_exists) continue;

    bool dest_is_winux_link =
        same_file(canonical_winuxcmd_path(root), dest) &&
        !same_path_name(canonical_winuxcmd_path(root), dest);
    bool dest_is_legacy_link = is_legacy_link_name(dest.filename().string());
    if (dest_is_winux_link && dest_is_legacy_link) continue;

    if (dest_is_winux_link) {
      auto line = "wpm: destination is a WinuxCmd hardlink; use --force: " +
                  dest.string();
      safeErrorPrintLn(line);
      note(std::move(line));
      return 1;
    }

    ++already_present;
  }

  if (already_present == destinations->size() && !destinations->empty()) {
    if (!package_has_receipt(root, package)) {
      // Every destination exists, but wpm never installed this package:
      // the files came from another source. Report honestly instead of
      // claiming "already installed" (which skipped the download and the
      // sha256 verification).
      auto line =
          "wpm: '" + std::string(package) +
          "' destinations exist but are not wpm-managed; use --force to "
          "replace them with the indexed artifact";
      safeErrorPrintLn(line);
      note(std::move(line));
      return 1;
    }
    // Benign stop: in --json mode stdout stays pure, so this notice goes to
    // stderr; the result payload carries status "already_installed".
    auto line = "wpm: already installed " + std::string(package) +
                "; use --force to reinstall";
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
    return 0;
  }

  if (already_present > 0) {
    for (const auto& dest : *destinations) {
      std::error_code ec;
      if (fs::exists(dest, ec)) {
        auto line = "wpm: destination exists; use --force: " + dest.string();
        safeErrorPrintLn(line);
        note(std::move(line));
        return 1;
      }
    }
  }

  return std::nullopt;
}

auto files_equal(const fs::path& a, const fs::path& b) -> bool {
  std::error_code ec;
  if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
  std::ifstream ia(a, std::ios::binary);
  std::ifstream ib(b, std::ios::binary);
  if (!ia.is_open() || !ib.is_open()) return false;
  std::string sa((std::istreambuf_iterator<char>(ia)),
                 std::istreambuf_iterator<char>());
  std::string sb((std::istreambuf_iterator<char>(ib)),
                 std::istreambuf_iterator<char>());
  return sa == sb;
}

// The shim template ships next to wpm itself; fall back to a copy already
// present in usr\bin from a previous install.
auto package_command_count(const nlohmann::json& pkg) -> int {
  if (!pkg.contains("commands") || !pkg["commands"].is_array()) return 0;
  int count = 0;
  for (const auto& item : pkg["commands"]) {
    if (item.is_string()) ++count;
  }
  return count;
}

// Shim commands are hardlinks of winuxcmd.exe itself (same model as builtin
// command links): the dispatcher forwards non-builtin names to
// opt\<pkg>\<cmd>.exe, keeping single-binary distribution intact.
/// Where a shim-layout command's payload lands. The convention is
/// `opt\<pkg>\<command>.exe`, but a payload that must keep its own directory
/// layout (codex ships `bin/codex.exe` beside `codex-resources/`) declares the
/// destination in its `files` mapping instead - and the entry link for flat
/// layouts has always been resolved that way. Look in both places, in that
/// order, so a package never ends up with a shim that cannot find its payload.
auto shim_payload_for(const fs::path& root, const std::string& package,
                      const std::string& command, const nlohmann::json& artifact)
    -> fs::path {
  std::error_code ec;
  const fs::path conventional =
      package_payload_dir(root, package) / exe_suffix(command);
  if (fs::is_regular_file(conventional, ec)) return conventional;
  if (artifact.contains("files") && artifact["files"].is_array()) {
    for (const auto& mapping : artifact["files"]) {
      if (mapping_is_directory(mapping)) continue;
      auto destination =
          artifact_destination_for_mapping(root, artifact, package, mapping);
      if (!destination) continue;
      if (destination->filename() == exe_suffix(command)) return *destination;
    }
  }
  return conventional;
}

auto create_package_shims(const fs::path& root, const nlohmann::json& pkg,
                          bool force, bool dry_run, bool json) -> bool {
  const std::string package = pkg.value("name", "");
  const int commands = package_command_count(pkg);
  if (commands == 0) return true;

  if (dry_run) {
    // --json keeps stdout pure JSON; progress goes to stderr (#1156).
    auto line = wpm_text(
        "command.wpm.status.shims_dry_run",
        "wpm: dry-run: would create {} command shim(s) in usr/bin for '{}'",
        commands, package);
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
    return true;
  }

  fs::path self_exe = canonical_winuxcmd_path(root);
  std::error_code ec;
  if (!fs::is_regular_file(self_exe, ec)) {
    safeErrorPrintLn(wpm_text("command.wpm.error.shim_template",
                              "wpm: '{}' not found; shims were not created",
                              self_exe.string()));
    return false;
  }

  fs::path bin_dir = canonical_bin_dir(root);
  // A `layout: shim` command is a link to the forwarder when this installation
  // has one; the forwarder reads the payload path from the map instead of being
  // a whole dispatcher (unixwin/WinuxCmd#1207). Installations without it keep
  // the previous behaviour, so nothing breaks on an older root.
  const fs::path forwarder = package_shim_source(root);
  const fs::path shim_source = forwarder.empty() ? self_exe : forwarder;
  int created = 0;
  int unchanged = 0;
  for (const auto& item : pkg["commands"]) {
    if (!item.is_string()) continue;
    const std::string command = item.get<std::string>();
    fs::path shim = bin_dir / (command + ".exe");
    ec.clear();
    if (fs::exists(shim, ec)) {
      if (same_file(shim_source, shim) || files_equal(shim_source, shim)) {
        ++unchanged;
        continue;
      }
      if (!force) {
        safeErrorPrintLn("wpm: destination exists; use --force: " +
                         shim.string());
        return false;
      }
    }
    fs::create_directories(bin_dir, ec);
    // Hardlink-first keeps usr\bin uniform: every entry links exactly one
    // canonical file (builtins -> winuxcmd.exe, package shims -> the forwarder).
    if (!materialize_file(shim_source, shim, true, ec)) {
      safeErrorPrintLn(wpm_text("command.wpm.error.shim_create",
                                "wpm: failed to create shim '{}': {}",
                                shim.string(), ec.message()));
      return false;
    }
    if (!forwarder.empty()) {
      // Point the forwarder at this package's payload for this command.
      const auto artifact_for_shim = artifact_for_current_arch(pkg);
      const fs::path payload = artifact_for_shim
                                   ? shim_payload_for(root, package, command,
                                                      *artifact_for_shim)
                                   : package_payload_dir(root, package) /
                                         exe_suffix(command);
      if (fs::is_regular_file(payload, ec)) {
        const fs::path relative = fs::relative(payload, root, ec);
        if (!ec && set_shim_map_entry(root, command, relative.string()) == false) {
          safeErrorPrintLn(wpm_text("command.wpm.error.shim_map",
                                    "wpm: failed to record the payload for "
                                    "'{}' in {}",
                                    command, shim_map_path(root).string()));
          return false;
        }
      }
    }
    ++created;
  }
  // Alias creation moved to create_package_aliases() for use with both layouts
  return true;
}

auto create_package_aliases(const fs::path& root, const nlohmann::json& pkg,
                            bool force, bool dry_run, bool json) -> bool {
  const auto package = pkg.value("name", "");
  if (!pkg.contains("aliases") || !pkg["aliases"].is_array()) return true;
  const auto bin_dir = canonical_bin_dir(root);
  std::error_code ec;
  int created = 0;
  for (const auto& alias : pkg["aliases"]) {
    const auto alias_name =
        alias.is_string() ? alias.get<std::string>() : alias.value("name", "");
    auto target_name =
        alias.is_string() ? std::string{} : alias.value("target", "");
    if (target_name.empty() && pkg.contains("commands") &&
        pkg["commands"].is_array() && !pkg["commands"].empty() &&
        pkg["commands"][0].is_string())
      target_name = pkg["commands"][0].get<std::string>();
    if (alias_name.empty() || target_name.empty()) continue;
    // Alias should point to the installed executable, not the payload directory
    const fs::path source = bin_dir / exe_suffix(target_name);
    const fs::path destination = bin_dir / exe_suffix(alias_name);
    if (!fs::is_regular_file(source, ec)) {
      safeErrorPrintLn(wpm_text("command.wpm.error.alias_target",
                                "wpm: alias target not found for '{}'",
                                alias_name));
      return false;
    }
    ec.clear();
    if (fs::exists(destination, ec)) {
      if (same_file(source, destination)) continue;
      if (!force) {
        safeErrorPrintLn(
            wpm_text("command.wpm.error.alias_exists",
                     "wpm: alias destination exists; use --force: {}",
                     destination.string()));
        return false;
      }
      // Alias destination may also be a name of the running image; POSIX
      // unlink handles cluster links and plain files alike.
      if (!delete_file_name(destination)) return false;
    }
    if (dry_run) continue;
    if (!CreateHardLinkW(destination.wstring().c_str(),
                         source.wstring().c_str(), nullptr)) {
      safeErrorPrintLn(wpm_text("command.wpm.error.alias_create",
                                "wpm: failed to create alias '{}' -> '{}': {}",
                                destination.string(), source.string(),
                                win32_error_text(GetLastError())));
      return false;
    }
    ++created;
  }
  if (created > 0) {
    // --json keeps stdout pure JSON; progress goes to stderr (#1156).
    auto line =
        wpm_text("command.wpm.status.aliases_created",
                 "wpm: created {} alias(es) for '{}'", created, package);
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
  }
  return true;
}

// Migration: packages installed before the layout field existed put every
// file (DLLs included) directly into usr\bin. Reinstalling such a package as
// layout=shim would leave those stale copies behind. Remove a legacy flat
// file only when it is byte-identical to the staged payload — proving it is
// this package's own leftover, never another package's file.
auto cleanup_legacy_flat_artifacts(const fs::path& root,
                                   const fs::path& extracted,
                                   const nlohmann::json& artifact,
                                   const nlohmann::json& pkg, bool json)
    -> int {
  const std::string package = pkg.value("name", "");
  if (!artifact.contains("files") || !artifact["files"].is_array()) return 0;
  fs::path bin_dir = canonical_bin_dir(root);
  int removed = 0;
  for (const auto& mapping : artifact["files"]) {
    std::string from = mapping.value("from", "");
    bool directory = mapping_is_directory(mapping);
    if (directory) continue;
    std::string to = mapping.contains("to") && mapping["to"].is_string()
                         ? mapping["to"].get<std::string>()
                         : mapping_default_to(from, directory);
    if (from.empty() || to.empty() || to.find('/') != std::string::npos) {
      continue;
    }
    auto src = find_artifact_path(extracted, from, false, false);
    if (!src) continue;
    fs::path legacy = bin_dir / to;
    std::error_code ec;
    if (!fs::exists(legacy, ec)) continue;
    if (same_file(canonical_winuxcmd_path(root), legacy)) continue;
    if (!files_equal(*src, legacy)) continue;
    if (!DeleteFileW(legacy.wstring().c_str())) continue;
    ++removed;
  }
  if (removed > 0) {
    // --json keeps stdout pure JSON; progress goes to stderr (#1156).
    auto line =
        wpm_text("command.wpm.status.shim_migrated",
                 "wpm: removed {} stale flat file(s) from usr/bin for '{}'",
                 removed, package);
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
  }
  return removed;
}

auto update_index(const Options& opts, bool json_payload) -> int;

auto refresh_index_once(const Options& opts, std::string_view reason) -> bool {
  // --json keeps stdout pure JSON; progress goes to stderr (#1156).
  auto line =
      wpm_text("command.wpm.status.auto_index_update",
               "wpm: {}; updating index from configured sources", reason);
  if (opts.json) {
    safeErrorPrintLn(line);
  } else {
    safePrintLn(line);
  }
  if (update_index(opts, false) == 0) return true;
  safeErrorPrintLn(wpm_text("command.wpm.error.auto_index_failed",
                            "wpm: automatic index update failed"));
  safeErrorPrintLn(
      wpm_text("command.wpm.error.auto_index_hint",
               "wpm: run 'wpm index update' to retry or 'wpm source list' "
               "to inspect sources"));
  return false;
}

// Version ordering for index entries: a dotted-numeric core with an optional
// packaging revision ("5.4.1-unixwin2" > "5.4.1-unixwin1" > "5.4.1"), which is
// what a repackaged artifact needs (the upstream version stays, our rebuild
// gets a revision). Returns std::nullopt when either side does not follow that
// shape - the index also carries spellings such as "9.5.0.0p1", "r42" or
// "kustomize/v5.8.1" - and callers then keep the conservative "any difference
// is an update" rule instead of guessing an order.
auto compare_versions(std::string_view left, std::string_view right)
    -> std::optional<int> {
  auto parse_core = [](std::string_view text)
      -> std::optional<std::vector<unsigned long long>> {
    std::vector<unsigned long long> parts;
    size_t begin = 0;
    while (begin <= text.size()) {
      size_t dot = text.find('.', begin);
      std::string_view segment =
          dot == std::string_view::npos ? text.substr(begin)
                                       : text.substr(begin, dot - begin);
      if (segment.empty() || segment.size() > 18) return std::nullopt;
      unsigned long long value = 0;
      for (char ch : segment) {
        if (ch < '0' || ch > '9') return std::nullopt;
        value = value * 10 + static_cast<unsigned long long>(ch - '0');
      }
      parts.push_back(value);
      if (dot == std::string_view::npos) break;
      begin = dot + 1;
    }
    return parts;
  };
  auto split_revision = [](std::string_view text)
      -> std::pair<std::string_view, std::string_view> {
    const size_t dash = text.find('-');
    if (dash == std::string_view::npos) return {text, {}};
    return {text.substr(0, dash), text.substr(dash + 1)};
  };
  auto compare_core = [](const std::vector<unsigned long long>& lhs,
                         const std::vector<unsigned long long>& rhs) -> int {
    const size_t count = std::max(lhs.size(), rhs.size());
    for (size_t i = 0; i < count; ++i) {
      const unsigned long long l = i < lhs.size() ? lhs[i] : 0;
      const unsigned long long r = i < rhs.size() ? rhs[i] : 0;
      if (l != r) return l < r ? -1 : 1;
    }
    return 0;
  };

  const auto [left_core, left_revision] = split_revision(left);
  const auto [right_core, right_revision] = split_revision(right);
  auto lhs = parse_core(left_core);
  auto rhs = parse_core(right_core);
  if (!lhs || !rhs) return std::nullopt;
  if (const int core = compare_core(*lhs, *rhs); core != 0) return core;
  if (left_revision.empty() && right_revision.empty()) return 0;
  // A repackaging revision outranks the bare upstream version.
  if (left_revision.empty()) return -1;
  if (right_revision.empty()) return 1;

  auto tokens = [](std::string_view text) {
    std::vector<std::string_view> out;
    size_t begin = 0;
    while (begin <= text.size()) {
      size_t end = text.find_first_of(".-", begin);
      std::string_view token = end == std::string_view::npos
                                   ? text.substr(begin)
                                   : text.substr(begin, end - begin);
      if (!token.empty()) out.push_back(token);
      if (end == std::string_view::npos) break;
      begin = end + 1;
    }
    return out;
  };
  const auto left_tokens = tokens(left_revision);
  const auto right_tokens = tokens(right_revision);
  for (size_t i = 0; i < std::min(left_tokens.size(), right_tokens.size()); ++i) {
    const auto l = parse_core(left_tokens[i]);
    const auto r = parse_core(right_tokens[i]);
    int order = 0;
    if (l && r) {
      order = compare_core(*l, *r);
    } else {
      const int cmp = left_tokens[i].compare(right_tokens[i]);
      order = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
    }
    if (order != 0) return order;
  }
  if (left_tokens.size() != right_tokens.size())
    return left_tokens.size() < right_tokens.size() ? -1 : 1;
  return 0;
}

// True when `path` resolves strictly inside `root` (guards receipt-driven
// deletions against a receipt written for a different root).
auto path_is_within(const fs::path& root, const fs::path& path) -> bool {
  std::error_code ec;
  const fs::path relative = fs::relative(path, root, ec);
  if (ec) return false;
  const std::string text = relative.generic_string();
  return !text.empty() && text != "." && text != ".." &&
         !text.starts_with("../");
}

// Remove files under `installed` that the artifact source tree `source` no
// longer ships, then drop the directories that became empty. Only used on a
// version change, so a same-version reinstall does not pay for the walk.
auto prune_stale_entries(const fs::path& installed, const fs::path& source,
                         bool dry_run, bool verbose) -> void {
  std::error_code ec;
  std::vector<fs::path> directories;
  fs::recursive_directory_iterator it(
      installed, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const fs::path entry = it->path();
    std::error_code type_ec;
    if (it->is_directory(type_ec)) {
      directories.push_back(entry);
      continue;
    }
    if (!it->is_regular_file(type_ec)) continue;
    std::error_code rel_ec;
    const fs::path relative = fs::relative(entry, installed, rel_ec);
    if (rel_ec) continue;
    std::error_code exists_ec;
    if (fs::exists(source / relative, exists_ec)) continue;
    if (dry_run) {
      if (verbose)
        safePrintLn("wpm: would remove superseded file " + entry.string());
      continue;
    }
    if (delete_file_name(entry) && verbose)
      safePrintLn("wpm: removed superseded file " + entry.string());
  }
  if (dry_run) return;
  std::sort(directories.begin(), directories.end(),
            [](const fs::path& a, const fs::path& b) {
              return std::distance(a.begin(), a.end()) >
                     std::distance(b.begin(), b.end());
            });
  for (const auto& directory : directories) {
    std::error_code empty_ec;
    if (!fs::is_empty(directory, empty_ec) || empty_ec) continue;
    std::error_code rm_ec;
    fs::remove(directory, rm_ec);
  }
}

// Reconcile the package's previous install with what this artifact ships:
// destinations the new mapping no longer covers are removed, and on a version
// change files inside still-covered directories that the new payload dropped
// are pruned too. Without it a layout change leaves inert leftovers behind -
// most visibly the retired shim launcher opt/<pkg>/<cmd>.exe after a move to
// layout:flat (unixwin/niu-git#8) or a stale DLL in a PATH directory from an
// older flat packaging. Only paths this package's own receipt recorded are
// considered, they must resolve inside the root, and the winuxcmd image and
// the running executable are never touched.
auto prune_superseded_install(const fs::path& root, const std::string& package,
                              const nlohmann::json& pkg,
                              const nlohmann::json& artifact,
                              const fs::path& extracted, bool dry_run,
                              bool verbose) -> void {
  if (package.empty()) return;
  auto text = read_text(receipt_path(root, package));
  if (!text) return;
  auto receipt = parse_json_text(*text);
  if (!receipt || !receipt->contains("destinations") ||
      !(*receipt)["destinations"].is_array())
    return;

  const fs::path core = canonical_winuxcmd_path(root);
  const fs::path current = current_exe_path();

  struct Covered {
    fs::path destination;
    fs::path source;
    bool directory = false;
  };
  std::vector<Covered> covered;
  if (artifact.contains("files") && artifact["files"].is_array()) {
    for (const auto& mapping : artifact["files"]) {
      const bool directory = mapping_is_directory(mapping);
      auto destination =
          artifact_destination_for_mapping(root, artifact, package, mapping);
      if (!destination) continue;
      const std::string from = mapping.value("from", "");
      const bool single_file_fallback = artifact.value("type", "exe") == "exe";
      auto source =
          find_artifact_path(extracted, from, directory, single_file_fallback);
      covered.push_back({*destination, source ? *source : fs::path{}, directory});
    }
  }
  // The entry links this artifact expects are covered destinations: without
  // this the reconcile would treat the previous receipt's `bin/<cmd>` entries
  // as "no longer mapped" and delete them.
  for (const auto& [payload, link] : package_entry_links(root, pkg, artifact)) {
    (void)payload;
    covered.push_back({link, fs::path{}, false});
  }

  const bool upgraded =
      receipt->value("version", "") != artifact.value("version", std::string{});
  for (const auto& entry : (*receipt)["destinations"]) {
    if (!entry.is_string()) continue;
    const fs::path previous(entry.get<std::string>());
    std::error_code exists_ec;
    if (!fs::exists(previous, exists_ec)) continue;
    if (!path_is_within(root, previous)) continue;
    if (same_file(core, previous) || same_path_name(current, previous))
      continue;

    const Covered* match = nullptr;
    for (const auto& candidate : covered) {
      if (normalized_path_key(candidate.destination) ==
          normalized_path_key(previous)) {
        match = &candidate;
        break;
      }
    }
    if (match != nullptr) {
      if (upgraded && match->directory && !match->source.empty())
        prune_stale_entries(previous, match->source, dry_run, verbose);
      continue;
    }

    // Not part of the artifact any more: drop the file or the whole directory.
    std::error_code type_ec;
    const bool directory = fs::is_directory(previous, type_ec);
    if (dry_run) {
      if (verbose)
        safePrintLn("wpm: would remove superseded path " + previous.string());
      continue;
    }
    std::error_code remove_ec;
    if (directory) {
      fs::remove_all(previous, remove_ec);
    } else {
      std::error_code direct_ec;
      fs::remove(previous, direct_ec);
      if (direct_ec) {
        // Held by a live process: park it instead of aborting the upgrade, and
        // let the cleanup take the bytes once the holder is gone.
        fs::path parked;
        if (move_aside(previous, &parked)) {
          std::error_code parked_ec;
          fs::remove(parked, parked_ec);
          if (verbose)
            safePrintLn("wpm: parked superseded path " + previous.string());
        } else {
          remove_ec = direct_ec;
        }
      }
    }
    if (remove_ec) {
      safeErrorPrintLn("wpm: failed to remove superseded path '" +
                       previous.string() + "': " + remove_ec.message());
    } else if (verbose) {
      safePrintLn("wpm: removed superseded path " + previous.string());
    }
  }
}

auto install_package(const Options& opts, std::string_view package_name,
                     nlohmann::json* json_result) -> int {
  // When json_result is non-null (--json mode) stdout stays pure JSON: human
  // progress moves to stderr and this call records one result object
  // (mirrors uninstall_packages, #1156).
  const bool json = json_result != nullptr;
  auto progress = [json](std::string_view line) {
    if (json) {
      safeErrorPrintLn(line);
    } else {
      safePrintLn(line);
    }
  };
  std::vector<std::string> json_errors;
  const std::string requested(package_name);
  auto index = load_index(opts.root);
  auto pkg = find_package(index, package_name);
  // finish() records the result object once; every exit path goes through it.
  auto finish = [&](bool ok, const std::string& status) -> int {
    if (json_result != nullptr) {
      *json_result = {{"name", requested},
                      {"status", status},
                      {"dry_run", opts.dry_run},
                      {"errors", json_errors}};
      if (ok && pkg) (*json_result)["version"] = pkg->value("version", "");
    }
    return ok ? 0 : 1;
  };

  bool refreshed = false;
  if (!pkg) {
    refreshed = refresh_index_once(opts, "package not found in local index");
    if (refreshed) {
      index = load_index(opts.root);
      pkg = find_package(index, package_name);
    }
  }
  if (!pkg) {
    auto line =
        wpm_text("command.wpm.error.install_not_found",
                 "wpm: package not found after index update: {}", package_name);
    safeErrorPrintLn(line);
    json_errors.push_back(std::move(line));
    return finish(false, "not_found");
  }

  // Follow `alias_of` before anything looks at artifacts: an alias entry has
  // none on purpose.
  if (const auto resolved = resolve_package_alias(index, requested);
      resolved.name != requested) {
    if (resolved.cycle) {
      auto line = wpm_text("command.wpm.error.alias_cycle",
                           "wpm: '{}' is part of an alias cycle: {}", requested,
                           format_package_alias_chain(resolved.chain));
      safeErrorPrintLn(line);
      json_errors.push_back(std::move(line));
      return finish(false, "alias_cycle");
    }
    pkg = find_package(index, resolved.name);
    if (!pkg && !refreshed) {
      refreshed = refresh_index_once(opts, "alias target is not in the index");
      if (refreshed) {
        index = load_index(opts.root);
        pkg = find_package(index, resolved.name);
      }
    }
    if (!pkg) {
      auto line = wpm_text("command.wpm.error.alias_target",
                           "wpm: '{}' is an alias for '{}', which is not in the "
                           "index",
                           requested, resolved.name);
      safeErrorPrintLn(line);
      json_errors.push_back(std::move(line));
      return finish(false, "alias_target_missing");
    }
    progress(wpm_text("command.wpm.status.package_alias",
                      "wpm: '{}' is provided by '{}'", requested,
                      resolved.name));
    if (json_result != nullptr) (*json_result)["alias_of"] = resolved.name;
  }

  auto state = artifact_install_state(*pkg);
  if (state != "ready" && !refreshed) {
    refreshed = refresh_index_once(opts, "package metadata is not installable");
    if (refreshed) {
      index = load_index(opts.root);
      pkg = find_package(index, package_name);
      if (pkg) state = artifact_install_state(*pkg);
    }
  }
  if (state != "ready") {
    auto line = wpm_text("command.wpm.error.not_installable",
                         "wpm: package is not installable for {}: {}",
                         detect_arch_key(), state);
    safeErrorPrintLn(line);
    json_errors.push_back(std::move(line));
    json_errors.push_back(
        wpm_text("command.wpm.error.fix_index_hint",
                 "wpm: update the source index artifact urls, sha256, and "
                 "files mapping"));
    return finish(false, "not_installable");
  }

  auto artifact = artifact_for_current_arch(*pkg);
  if (!artifact) return finish(false, "error");
  // Packages linked against the MSYS2 runtime need msys-2.0.dll and friends;
  // in a pure Win32 layout they only run where that runtime is reachable.
  // Surface the index's runtime annotation before the user downloads.
  const std::string runtime =
      artifact->value("runtime", pkg->value("runtime", std::string{}));
  if (runtime == "msys" || runtime == "msys2") {
    safeErrorPrintLn(wpm_text(
        "command.wpm.warn.msys_runtime",
        "wpm: note: {} is linked against the MSYS2 runtime (msys-2.0.dll); "
        "it only runs where that runtime is available",
        pkg->value("name", std::string(package_name))));
  }
  std::vector<std::string> preflight_messages;
  auto preflight = preflight_install_destinations(
      opts.root, *artifact, pkg->value("name", std::string(package_name)),
      opts.force, json, json ? &preflight_messages : nullptr);
  if (preflight) {
    if (*preflight == 0) return finish(true, "already_installed");
    for (auto& message : preflight_messages)
      json_errors.push_back(std::move(message));
    return finish(false, "preflight_failed");
  }

  auto downloaded =
      download_artifact(opts.root, pkg->value("name", ""),
                        pkg->value("version", ""), *artifact,
                        opts.verbose, user_forced_proxy(opts), json);
  if (!downloaded) {
    json_errors.push_back(
        wpm_text("command.wpm.error.download_failed_generic",
                 "wpm: failed to download '{}' from every configured URL",
                 pkg->value("name", std::string(package_name))));
    return finish(false, "download_failed");
  }

  fs::path extracted = staging_dir(opts.root) / pkg->value("name", "package");
  std::error_code ec;
  fs::remove_all(extracted, ec);
  fs::create_directories(extracted, ec);

  std::string type = artifact->value("type", "exe");
  if (archive_artifact_type(type)) {
    if (!extract_archive(*downloaded, extracted, type, json)) {
      return finish(false, "error");
    }
  } else if (type == "exe") {
    if (!materialize_file(*downloaded, extracted / downloaded->filename(), true,
                          ec)) {
      auto line = wpm_text("command.wpm.error.stage",
                           "wpm: failed to stage exe: {}", ec.message());
      safeErrorPrintLn(line);
      json_errors.push_back(std::move(line));
      return finish(false, "error");
    }
  } else {
    auto line = wpm_text("command.wpm.error.unsupported_type",
                         "wpm: unsupported artifact type: {}", type);
    safeErrorPrintLn(line);
    json_errors.push_back(std::move(line));
    return finish(false, "error");
  }

  // Park every destination before writing anything. A file held by a running
  // process cannot be deleted or overwritten - but it can be *renamed* aside,
  // which is how a package with a live fsmonitor daemon still upgrades - and a
  // destination that cannot even be renamed refuses the install up front, with
  // the culprit named, instead of failing halfway and leaving the package
  // half-updated.
  ReplacementStaging staging;
  if (!opts.dry_run) {
    staging = stage_replacements(
        extracted, opts.root, *artifact,
        pkg->value("name", std::string(package_name)), *pkg);
    if (!staging.blocked.empty()) {
      std::string detail;
      for (const auto& item : staging.blocked) {
        detail += "\n  " + item.target.string();
        if (item.holders.empty()) {
          detail += "\n      (held by a process that cannot be named)";
        }
        for (const auto& holder : item.holders)
          detail += "\n      running: " + holder;
      }
      auto line = wpm_text(
          "command.wpm.error.files_in_use",
          "wpm: {} file(s) are in use and cannot be replaced; close the listed "
          "programs (a running git, an editor's git integration, a fsmonitor "
          "daemon) and retry:{}",
          staging.blocked.size(), detail);
      safeErrorPrintLn(line);
      json_errors.push_back(line);
      return finish(false, "files_in_use");
    }
  }
  const bool shim_layout = artifact_layout(*artifact) == "shim";
  // Reconcile the previous install before laying the new payload down, so a
  // renamed/removed file cannot survive as an inert leftover.
  prune_superseded_install(opts.root, pkg->value("name", std::string(package_name)),
                           *pkg, *artifact, extracted, opts.dry_run, opts.verbose);
  progress(wpm_text("command.wpm.status.installing_files",
                    "wpm: installing files..."));
  if (!copy_artifact_files(
          extracted, opts.root, *artifact, opts.force, opts.dry_run,
          pkg->value("name", std::string(package_name)), json)) {
    json_errors.push_back(
        wpm_text("command.wpm.error.copy_failed",
                 "wpm: failed to install one or more files for '{}'",
                 pkg->value("name", std::string(package_name))));
    return finish(false, "error");
  }
  if (shim_layout && !opts.dry_run) {
    cleanup_legacy_flat_artifacts(opts.root, extracted, *artifact, *pkg, json);
  }
  if (shim_layout) {
    if (!create_package_shims(opts.root, *pkg, opts.force, opts.dry_run,
                              json)) {
      json_errors.push_back(
          wpm_text("command.wpm.error.shim_failed",
                   "wpm: failed to create command shims for '{}'",
                   pkg->value("name", std::string(package_name))));
      return finish(false, "error");
    }
  }
  if (!create_package_aliases(opts.root, *pkg, opts.force, opts.dry_run,
                              json)) {
    json_errors.push_back(
        wpm_text("command.wpm.error.alias_failed",
                 "wpm: failed to create command aliases for '{}'",
                 pkg->value("name", std::string(package_name))));
    return finish(false, "error");
  }
  if (opts.dry_run) {
    progress(wpm_text("command.wpm.status.install_dry_run",
                      "wpm: dry-run complete; would install {}",
                      pkg->value("name", std::string(package_name))));
    return finish(true, "would_install");
  }
  // The host PATH points at <root>/bin, not <root>/usr/bin (that directory
  // carries hundreds of tools and stays off PATH): give every command of this
  // package an entry link there, as a hardlink to the payload binary so the
  // cost is zero and no launcher process is involved (unixwin/niubash#376).
  if (!create_package_entry_links(opts.root, *pkg, *artifact, opts.force,
                                  opts.dry_run, json)) {
    json_errors.push_back(
        wpm_text("command.wpm.error.entry_link_failed",
                 "wpm: failed to create entry links for '{}'",
                 pkg->value("name", std::string(package_name))));
    return finish(false, "error");
  }
  write_install_receipt(opts.root, *pkg, *artifact);
  discard_parked(staging, opts.verbose);
  // The extraction directory has served its purpose and would otherwise keep a
  // second copy of the payload - and, historically, the inode the PATH entry
  // pointed at - alive under a directory that reads as transient.
  std::error_code prune_ec;
  fs::remove_all(extracted, prune_ec);
  if (prune_ec && opts.verbose)
    safeErrorPrintLn("wpm: note: could not prune " + extracted.string() + ": " +
                     prune_ec.message());
  // The success line is the only confirmation a plain install prints, so it
  // carries the name, the version and the primary destination (#1363: a
  // bare "installed <name>" left the user unable to tell what landed where).
  const std::string installed_name =
      pkg->value("name", std::string(package_name));
  std::string installed_dest = opts.root.string();
  if (auto destinations =
          artifact_destination_paths(opts.root, *artifact, installed_name);
      destinations && !destinations->empty()) {
    installed_dest = destinations->front().string();
  }
  progress(wpm_text("command.wpm.status.installed",
                    "wpm: installed {} {} -> {}", installed_name,
                    pkg->value("version", ""), installed_dest));
  return finish(true, "installed");
}

auto install_packages(const Options& opts,
                      std::span<const std::string_view> packages) -> int {
  int failed = 0;
  nlohmann::json results = nlohmann::json::array();
  for (const auto package : packages) {
    nlohmann::json result;
    if (install_package(opts, package, opts.json ? &result : nullptr) != 0) {
      ++failed;
    }
    if (opts.json) {
      if (!result.is_object()) {
        result = {{"name", std::string(package)},
                  {"status", "error"},
                  {"dry_run", opts.dry_run},
                  {"errors", nlohmann::json::array()}};
      }
      results.push_back(std::move(result));
    }
  }
  if (opts.json) {
    nlohmann::json payload = {
        {"schema", 1}, {"results", std::move(results)}, {"failed", failed}};
    safePrintLn(payload.dump(2));
    return failed == 0 ? 0 : 1;
  }
  if (packages.size() > 1) {
    safePrintLn(wpm_text("command.wpm.status.install_summary",
                         "wpm: install summary: requested={} failed={}",
                         packages.size(), failed));
  }
  return failed == 0 ? 0 : 1;
}

auto strip_manifest_comment(std::string_view line) -> std::string_view {
  bool in_string = false;
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"') in_string = !in_string;
    if (line[i] == '#' && !in_string) return line.substr(0, i);
  }
  return line;
}

auto parse_toml_string_array(std::string_view value)
    -> std::optional<std::vector<std::string>> {
  std::vector<std::string> items;
  size_t i = 0;
  while (i < value.size()) {
    if (value[i] == '"') {
      std::string out;
      size_t j = i + 1;
      bool closed = false;
      while (j < value.size()) {
        if (value[j] == '\\' && j + 1 < value.size() &&
            (value[j + 1] == '"' || value[j + 1] == '\\')) {
          out += value[j + 1];
          j += 2;
          continue;
        }
        if (value[j] == '"') {
          closed = true;
          ++j;
          break;
        }
        out += value[j++];
      }
      if (!closed) return std::nullopt;
      items.push_back(std::move(out));
      i = j;
      continue;
    }
    if (value[i] == ']') break;
    ++i;
  }
  return items;
}

auto unquote_toml_string(std::string_view token) -> std::optional<std::string> {
  token = trim_ascii(std::string(token));
  if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
    return std::nullopt;
  }
  auto items = parse_toml_string_array(token);
  if (!items || items->size() != 1) return std::nullopt;
  return std::move((*items)[0]);
}

// Minimal manifest reader for `wpm install --from`. Accepts either JSON
// {"packages": {"wpm": ["a", "b"]}} or a TOML subset with a [packages]
// section declaring wpm = ["a", "b"] (single or multiline array).
auto parse_wpm_manifest(std::string_view text)
    -> std::optional<std::vector<std::string>> {
  auto trimmed_text = trim_ascii(std::string(text));
  if (!trimmed_text.empty() && trimmed_text.front() == '{') {
    auto parsed = parse_json_text(trimmed_text);
    if (!parsed) return std::nullopt;
    if (parsed->contains("packages") && (*parsed)["packages"].is_object() &&
        (*parsed)["packages"].contains("wpm") &&
        (*parsed)["packages"]["wpm"].is_array()) {
      std::vector<std::string> items;
      for (const auto& item : (*parsed)["packages"]["wpm"]) {
        if (item.is_string()) items.push_back(item.get<std::string>());
      }
      return items;
    }
    return std::nullopt;
  }

  std::vector<std::string> items;
  bool in_packages = false;
  bool found = false;
  bool saw_structure = false;
  std::string pending_key;
  std::string pending_value;

  auto finish_array = [&](std::string_view value) -> bool {
    auto parsed_items = parse_toml_string_array(value);
    if (!parsed_items) return false;
    if (pending_key == "wpm" && in_packages) {
      for (auto& item : *parsed_items) items.push_back(std::move(item));
      found = true;
    }
    pending_key.clear();
    pending_value.clear();
    return true;
  };

  std::istringstream in{std::string(text)};
  std::string line;
  while (std::getline(in, line)) {
    auto content = trim_ascii(std::string(strip_manifest_comment(line)));
    if (content.empty()) continue;

    if (content.front() == '[' && content.back() == ']' &&
        pending_key.empty()) {
      auto section = trim_ascii(content.substr(1, content.size() - 2));
      in_packages = section == "packages";
      saw_structure = true;
      continue;
    }

    auto eq = content.find('=');
    if (eq == std::string::npos) continue;
    saw_structure = true;

    if (!pending_key.empty()) {
      pending_value += " " + content;
      if (content.find(']') != std::string::npos) {
        if (!finish_array(pending_value)) return std::nullopt;
      }
      continue;
    }

    std::string key = trim_ascii(content.substr(0, eq));
    std::string value = trim_ascii(content.substr(eq + 1));
    if (value.find('[') == std::string::npos) {
      if (key == "wpm" && in_packages) {
        if (auto single = unquote_toml_string(value)) {
          items.push_back(std::move(*single));
          found = true;
        }
      }
      continue;
    }
    pending_key = key;
    pending_value = value.substr(value.find('[') + 1);
    if (pending_value.find(']') != std::string::npos) {
      if (!finish_array(pending_value)) return std::nullopt;
    }
  }
  if (found) return items;
  // Plain package list (one name per line, as written by `wpm export
  // --plain`) is accepted as a fallback when no TOML/JSON structure exists.
  if (saw_structure) return std::nullopt;
  for (const auto& raw_line : std::views::split(std::string(text), '\n')) {
    auto entry = trim_ascii(std::string(raw_line.begin(), raw_line.end()));
    if (entry.empty() || entry.starts_with('#')) continue;
    items.push_back(std::move(entry));
  }
  return items.empty() ? std::nullopt : std::optional{std::move(items)};
}

auto install_from_manifest(const Options& opts, const std::string& from)
    -> int {
  std::string text;
  if (starts_with_ci(from, "http://") || starts_with_ci(from, "https://") ||
      starts_with_ci(from, "file://")) {
    auto result = http_get(from,
                           wpm_text("command.wpm.status.fetching_manifest",
                                    "wpm: fetching manifest from {}", from),
                           5, user_forced_proxy(opts),
                           loopback_fallback_allowed(opts.root));
    if (!result.ok) {
      safeErrorPrintLn(wpm_text("command.wpm.error.manifest_fetch",
                                "wpm: failed to fetch manifest '{}': {}", from,
                                result.error));
      return 1;
    }
    text.assign(reinterpret_cast<const char*>(result.data.data()),
                result.data.size());
  } else {
    std::ifstream in{fs::path(from), std::ios::binary};
    if (!in.is_open()) {
      safeErrorPrintLn(wpm_text("command.wpm.error.manifest_open",
                                "wpm: cannot open manifest '{}'", from));
      return 1;
    }
    text.assign(std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());
  }

  auto packages = parse_wpm_manifest(text);
  if (!packages || packages->empty()) {
    safeErrorPrintLn(wpm_text("command.wpm.error.manifest_empty",
                              "wpm: manifest '{}' has no [packages] wpm list",
                              from));
    return 1;
  }

  std::vector<std::string_view> views;
  views.reserve(packages->size());
  for (const auto& package : *packages) views.push_back(package);
  return install_packages(opts, views);
}

/// Where the detached update helper writes what it did. Its output used to be
/// discarded, which made every failure in the self-update path invisible -
/// including the one that leaves a command link on the previous image
/// (unixwin/WinuxCmd#1206).
auto update_log_path(const fs::path& root) -> fs::path {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04u%02u%02u-%02u%02u%02u",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                now.wSecond);
  return state_dir(root) / "logs" /
         ("self-update-" + std::string(stamp) + "-" +
          std::to_string(GetCurrentProcessId()) + ".log");
}

/// Keep the newest few update logs; each one is small and they are only read
/// when something went wrong.
void prune_update_logs(const fs::path& root, size_t keep) {
  std::error_code ec;
  const fs::path dir = state_dir(root) / "logs";
  std::vector<fs::path> logs;
  for (const auto& entry : fs::directory_iterator(
           dir, fs::directory_options::skip_permission_denied, ec)) {
    const auto name = entry.path().filename().string();
    if (name.rfind("self-update-", 0) == 0 && entry.path().extension() == ".log")
      logs.push_back(entry.path());
  }
  if (logs.size() <= keep) return;
  std::sort(logs.begin(), logs.end());  // timestamped names sort chronologically
  for (size_t i = 0; i + keep < logs.size(); ++i) {
    std::error_code remove_ec;
    fs::remove(logs[i], remove_ec);
  }
}

auto launch_apply_update(const fs::path& staged_exe, const fs::path& root,
                         DWORD parent_pid,
                         const std::optional<fs::path>& staged_forwarder)
    -> bool {
  std::wstring cmd = quote(staged_exe) + L" wpm -- __apply-update --root " +
                     quote(root) + L" --payload " + quote(staged_exe) +
                     L" --parent " + std::to_wstring(parent_pid);
  if (staged_forwarder)
    cmd += L" --forwarder " + quote(*staged_forwarder);

  // Give the detached helper somewhere to speak: everything it prints - and
  // anything it fails at - goes to this log instead of nowhere.
  std::error_code log_ec;
  const fs::path log = update_log_path(root);
  fs::create_directories(log.parent_path(), log_ec);
  SECURITY_ATTRIBUTES inherit{};
  inherit.nLength = sizeof(inherit);
  inherit.bInheritHandle = TRUE;
  HANDLE log_handle =
      CreateFileW(log.wstring().c_str(), FILE_APPEND_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL, nullptr);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  if (log_handle != INVALID_HANDLE_VALUE) {
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = log_handle;
    si.hStdError = log_handle;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(staged_exe.wstring().c_str(), cmd.data(), nullptr,
                           nullptr, log_handle != INVALID_HANDLE_VALUE ? TRUE : FALSE,
                           CREATE_NO_WINDOW, nullptr, root.wstring().c_str(), &si,
                           &pi);
  if (log_handle != INVALID_HANDLE_VALUE) CloseHandle(log_handle);
  if (!ok) {
    safeErrorPrintLn("wpm: failed to launch update helper: " +
                     win32_error_text(GetLastError()));
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  prune_update_logs(root, 3);
  if (log_handle != INVALID_HANDLE_VALUE) {
    safePrintLn(wpm_text("command.wpm.status.update_log",
                         "wpm: update helper log: {}", log.string()));
  }
  return true;
}

auto update_winuxcmd(const Options& opts) -> int {
  if (!refresh_index_once(opts,
                          "checking for latest winuxcmd package metadata")) {
    return 1;
  }

  auto index = load_index(opts.root);
  auto pkg = find_package(index, "winuxcmd");
  if (!pkg) {
    safeErrorPrintLn(wpm_text("command.wpm.error.core_missing",
                              "wpm: winuxcmd package missing after index "
                              "update"));
    return 1;
  }

  auto state = artifact_install_state(*pkg);
  if (state != "ready") {
    safeErrorPrintLn(wpm_text("command.wpm.error.not_installable",
                              "wpm: package is not installable for {}: {}",
                              detect_arch_key(), state));
    return 1;
  }

  auto artifact = artifact_for_current_arch(*pkg);
  if (!artifact) return 1;
  auto downloaded = download_artifact(
      opts.root, "winuxcmd", pkg->value("version", ""), *artifact,
      opts.verbose, user_forced_proxy(opts), opts.json);
  if (!downloaded) return 1;

  fs::path extracted = staging_dir(opts.root) / "winuxcmd-update";
  std::error_code ec;
  fs::remove_all(extracted, ec);
  fs::create_directories(extracted, ec);

  std::string type = artifact->value("type", "zip");
  if (archive_artifact_type(type)) {
    if (!extract_archive(*downloaded, extracted, type, opts.json)) return 1;
  } else if (type == "exe") {
    fs::copy_file(*downloaded, extracted / "winuxcmd.exe",
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
      safeErrorPrintLn(wpm_text("command.wpm.error.stage_core",
                                "wpm: failed to stage winuxcmd.exe: {}",
                                ec.message()));
      return 1;
    }
  } else {
    safeErrorPrintLn(wpm_text("command.wpm.error.unsupported_type",
                              "wpm: unsupported artifact type: {}", type));
    return 1;
  }

  auto staged_exe = find_file_recursive(extracted, "winuxcmd.exe");
  if (!staged_exe) {
    safeErrorPrintLn(wpm_text("command.wpm.error.staged_missing",
                              "wpm: staged winuxcmd.exe not found"));
    return 1;
  }

  // The release carries the package-shim forwarder next to the image (the
  // install rules put both into usr/bin). The index `files` mapping may not
  // list it yet, so self-update stages it explicitly: without this, an updated
  // installation keeps paying the old winuxcmd dispatcher for every
  // `layout: shim` command (unixwin/WinuxCmd#1207).
  const auto staged_forwarder =
      find_file_recursive(extracted, "winuxcmd-shim.exe");

  if (opts.dry_run) {
    safePrintLn(wpm_text("command.wpm.status.update_dry_run",
                         "wpm: dry-run: would apply update from {}",
                         staged_exe->string()));
    return 0;
  }

  if (!launch_apply_update(*staged_exe, opts.root, GetCurrentProcessId(),
                           staged_forwarder)) {
    return 1;
  }
  safePrintLn(wpm_text("command.wpm.status.update_staged",
                       "wpm: update staged; helper will replace winuxcmd "
                       "after exit"));
  return 0;
}

auto parse_hidden_value(std::span<const std::string_view> args,
                        std::string_view name) -> std::string {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == name) return std::string(args[i + 1]);
  }
  return {};
}

/// Keep the newest few rollback copies. Every update writes one and nothing
/// ever pruned them, so a machine that has taken a handful of updates carries
/// every image it ever ran (unixwin/WinuxCmd#1208). The names carry a pid, not
/// a timestamp, so order by write time.
void prune_update_backups(const fs::path& root, size_t keep) {
  std::error_code ec;
  std::vector<std::pair<fs::file_time_type, fs::path>> backups;
  for (const auto& entry : fs::directory_iterator(
           backup_dir(root), fs::directory_options::skip_permission_denied, ec)) {
    const auto name = entry.path().filename().string();
    if (name.rfind("winuxcmd-before-update-", 0) != 0) continue;
    std::error_code time_ec;
    const auto written = fs::last_write_time(entry.path(), time_ec);
    if (time_ec) continue;
    backups.emplace_back(written, entry.path());
  }
  if (backups.size() <= keep) return;
  std::sort(backups.begin(), backups.end());
  for (size_t i = 0; i + keep < backups.size(); ++i) {
    std::error_code remove_ec;
    fs::remove(backups[i].second, remove_ec);  // best effort: a held image stays
  }
}

auto unique_update_backup_path(const fs::path& root) -> fs::path {
  fs::path dir = backup_dir(root);
  std::string stem =
      "winuxcmd-before-update-" + std::to_string(GetCurrentProcessId());
  fs::path candidate = dir / (stem + ".exe");
  std::error_code ec;
  for (int suffix = 1; fs::exists(candidate, ec); ++suffix) {
    candidate = dir / (stem + "-" + std::to_string(suffix) + ".exe");
    ec.clear();
  }
  return candidate;
}

/// Rebuild every command link onto the image that is now in place, then check
/// that they all moved. A single name left behind runs the previous build
/// forever, so the second attempt is deliberate and the residual names are
/// reported (unixwin/WinuxCmd#1206). The output lands in the helper's log.
auto finish_update(const fs::path& root) -> int {
  int rc = rebuild_links(root, true, false, false);
  auto stale = stale_command_links(root);
  if (stale.empty()) return rc;
  safeErrorPrintLn(wpm_text("command.wpm.status.links_retry",
                            "wpm: {} command link(s) still point at the "
                            "previous winuxcmd.exe; relinking once more",
                            stale.size()));
  rebuild_links(root, true, false, true);
  stale = stale_command_links(root);
  if (stale.empty()) return rc;
  safeErrorPrintLn(wpm_text("command.wpm.error.links_left_stale",
                            "wpm: update finished with {} command link(s) on "
                            "the previous winuxcmd.exe:",
                            stale.size()));
  for (const auto& name : stale)
    safeErrorPrintLn("  " + (canonical_bin_dir(root) / exe_suffix(name)).string());
  safeErrorPrintLn(
      "wpm: 'wpm links rebuild --force' retries them from the installed image");
  return 1;
}

auto apply_update(std::span<const std::string_view> args) -> int {
  fs::path root = parse_hidden_value(args, "--root");
  fs::path payload = parse_hidden_value(args, "--payload");
  std::string parent_text = parse_hidden_value(args, "--parent");
  fs::path forwarder = parse_hidden_value(args, "--forwarder");
  if (root.empty() || payload.empty() || parent_text.empty()) {
    safeErrorPrintLn("wpm: invalid apply-update invocation");
    return 1;
  }

  DWORD parent_pid = static_cast<DWORD>(std::stoul(parent_text));
  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
  if (parent) {
    WaitForSingleObject(parent, 30000);
    CloseHandle(parent);
  }

  // Land the staged package-shim forwarder in usr\bin before the links are
  // rebuilt: the rebuild is what points `layout: shim` commands at it, so the
  // image swap alone would leave every shim on the old dispatcher
  // (unixwin/WinuxCmd#1207). Best effort - a failure here is reported, but the
  // image update itself still stands.
  auto install_forwarder = [&]() {
    std::error_code forwarder_ec;
    if (forwarder.empty() ||
        !fs::is_regular_file(forwarder, forwarder_ec) || forwarder_ec)
      return;
    const fs::path destination = canonical_bin_dir(root) / "winuxcmd-shim.exe";
    if (fs::exists(destination, forwarder_ec) &&
        files_equal(forwarder, destination))
      return;
    // The shim may be mid-run somewhere; unlink the name first, like the image
    // swap does, instead of writing over it.
    if (fs::exists(destination, forwarder_ec)) delete_file_name(destination);
    fs::create_directories(destination.parent_path(), forwarder_ec);
    fs::copy_file(forwarder, destination, fs::copy_options::overwrite_existing,
                  forwarder_ec);
    if (forwarder_ec) {
      safeErrorPrintLn("wpm: failed to install winuxcmd-shim.exe: " +
                       forwarder_ec.message());
      return;
    }
    safePrintLn("wpm: installed the package-shim forwarder " +
                destination.string());
  };

  std::error_code ec;
  fs::create_directories(backup_dir(root), ec);
  // Keep one earlier rollback here, before this run writes its own: the fresh
  // copy below then leaves exactly two in the directory, which is the bound
  // #1208 accepts, and a swap that fails halfway still has a usable image to
  // restore from.
  prune_update_backups(root, 1);
  fs::path target = canonical_winuxcmd_path(root);
  fs::path backup = unique_update_backup_path(root);
  if (fs::exists(target, ec)) {
    fs::copy_file(target, backup, fs::copy_options::none, ec);
    if (ec) {
      safeErrorPrintLn("wpm: failed to backup winuxcmd.exe: " + ec.message());
      return 1;
    }
  }

  // Swap the name instead of overwriting the image in place. usr\bin entries
  // are hardlinks of this one record, and a mapped executable refuses an
  // in-place write ("being used by another process" - the failure that left
  // wpm update winuxcmd stuck with the old version on a live install), while
  // a POSIX unlink of the name always works and keeps already-running
  // processes on the old record. The new image is then moved into place and
  // every command link is rebuilt onto it.
  const fs::path staged_target =
      target.parent_path() /
      (target.stem().wstring() + L".update-" +
       std::to_wstring(GetCurrentProcessId()) + target.extension().wstring());
  std::error_code staged_ec;
  fs::copy_file(payload, staged_target, fs::copy_options::overwrite_existing,
                staged_ec);
  if (staged_ec) {
    safeErrorPrintLn("wpm: failed to stage the updated winuxcmd.exe: " +
                     staged_ec.message());
  } else if (delete_file_name(target)) {
    std::error_code rename_ec;
    fs::rename(staged_target, target, rename_ec);
    if (!rename_ec) {
      install_forwarder();
      return finish_update(root);
    }
    safeErrorPrintLn("wpm: failed to move the updated winuxcmd.exe into place: " +
                     rename_ec.message());
    // The old name is gone; put a usable image back before giving up.
    std::error_code restore_ec;
    fs::copy_file(backup, target, fs::copy_options::overwrite_existing,
                  restore_ec);
    if (restore_ec)
      safeErrorPrintLn("wpm: failed to restore backup winuxcmd.exe: " +
                       restore_ec.message());
    std::error_code cleanup_ec;
    fs::remove(staged_target, cleanup_ec);
    finish_update(root);
    return 1;
  } else {
    std::error_code cleanup_ec;
    fs::remove(staged_target, cleanup_ec);
    safeErrorPrintLn(
        "wpm: could not unlink the old winuxcmd.exe; falling back to an "
        "in-place replace");
  }

  ec.clear();
  fs::copy_file(payload, target, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    safeErrorPrintLn("wpm: failed to replace winuxcmd.exe: " + ec.message());
    std::error_code restore_ec;
    if (fs::exists(backup, ec)) {
      fs::copy_file(backup, target, fs::copy_options::overwrite_existing,
                    restore_ec);
      if (restore_ec) {
        safeErrorPrintLn("wpm: failed to restore backup winuxcmd.exe: " +
                         restore_ec.message());
      }
    }
    finish_update(root);
    return 1;
  }
  install_forwarder();
  return finish_update(root);
}

auto try_fetch_index(const Options& opts, std::string& used_source)
    -> std::optional<nlohmann::json> {
  auto sources = merged_sources(opts.root);
  auto config = load_config(opts.root);
  std::string wanted = opts.source.empty()
                           ? config.value("preferred_source", "auto")
                           : opts.source;

  // Source ordering only; every source is still tried on failure. An explicit
  // preferred_source goes first but the remaining sources stay as fallback —
  // a pinned source that dies (e.g. proxy turned off) must not strand the
  // client without any working index. region=cn always prefers regional
  // sources; region=auto probes the highest-priority source once and prefers
  // regional ones only when it is unreachable.
  if (wanted == "auto") {
    std::string region = config.value("region", "auto");
    bool prefer_cn = region == "cn";
    bool probed_unreachable = false;
    if (region == "auto") {
      bool has_cn = std::ranges::any_of(sources, [](const nlohmann::json& s) {
        return s.value("region", "") == "cn" && s.contains("index_urls") &&
               s["index_urls"].is_array() && !s["index_urls"].empty();
      });
      if (has_cn) {
        for (const auto& source : sources) {
          if (!source.contains("index_urls") ||
              !source["index_urls"].is_array() ||
              source["index_urls"].empty()) {
            continue;
          }
          if (source.value("region", "") == "cn") break;
          const auto& first = source["index_urls"].front();
          if (!first.is_string()) break;
          if (!probe_url(first.get<std::string>(), 3000,
                         user_forced_proxy(opts))) {
            prefer_cn = true;
            probed_unreachable = true;
          }
          break;
        }
      }
    }
    if (prefer_cn) {
      std::stable_partition(sources.begin(), sources.end(),
                            [](const nlohmann::json& s) {
                              return s.value("region", "") == "cn";
                            });
      if (probed_unreachable) {
        safePrintLn(wpm_text(
            "command.wpm.status.prefer_regional_sources",
            "wpm: global index source unreachable; trying regional mirrors "
            "first"));
      }
    }
  } else {
    std::stable_partition(
        sources.begin(), sources.end(),
        [&](const nlohmann::json& s) { return s.value("name", "") == wanted; });
  }

  bool preferred_seen = false;
  bool fallback_notified = false;
  for (const auto& source : sources) {
    std::string name = source.value("name", "");
    if (name.empty()) continue;
    if (wanted != "auto") {
      if (name == wanted) {
        preferred_seen = true;
      } else if (preferred_seen && !fallback_notified) {
        fallback_notified = true;
        safePrintLn(wpm_text(
            "command.wpm.status.preferred_fallback",
            "wpm: preferred source '{}' failed; trying other sources (run "
            "'wpm source use auto' to restore automatic selection)",
            wanted));
      }
    }
    if (!source.contains("index_urls") || !source["index_urls"].is_array())
      continue;
    for (const auto& url_json : source["index_urls"]) {
      if (!url_json.is_string()) continue;
      std::string url = url_json.get<std::string>();
      if (opts.verbose) safePrintLn("wpm: fetching index " + url);
      auto result = http_get(url,
                             wpm_text("command.wpm.status.fetching_index",
                                      "wpm: fetching index from {}", name),
                             5, user_forced_proxy(opts),
                             loopback_fallback_allowed(opts.root));
      if (!result.ok) {
        safeErrorPrintLn("wpm: index fetch failed from " + name + ": " +
                         result.error);
        continue;
      }
      std::string text(reinterpret_cast<const char*>(result.data.data()),
                       result.data.size());
      auto parsed = parse_json_text(text);
      if (!parsed || !parsed->contains("packages")) {
        safeErrorPrintLn("wpm: fetched index is missing packages");
        continue;
      }
      used_source = name;
      return parsed;
    }
  }
  return std::nullopt;
}

auto update_index(const Options& opts, bool json_payload) -> int {
  std::string used_source;
  auto fetched = try_fetch_index(opts, used_source);
  if (!fetched) {
    safeErrorPrintLn(wpm_text("command.wpm.error.no_source",
                              "wpm: no reachable index source"));
    return 1;
  }
  if (!write_text(local_index_path(opts.root), fetched->dump(2))) {
    safeErrorPrintLn(
        wpm_text("command.wpm.error.save_index", "wpm: failed to save index"));
    return 1;
  }
  auto config = load_config(opts.root);
  // Supply-chain ledger (#1176): remember which source produced the index
  // and the hash of the persisted content. The index carries the artifact
  // sha256 values, so a silent content change on a source other than the
  // preferred one is the signature of a poisoned mirror — surface it.
  auto index_hash = sha256_file(local_index_path(opts.root));
  const std::string previous_hash = config.value("last_index_sha256", "");
  const std::string previous_source = config.value("last_success_source", "");
  std::string preferred = opts.source.empty()
                              ? config.value("preferred_source", "auto")
                              : opts.source;
  if (preferred.empty() || preferred == "auto") preferred = previous_source;
  if (index_hash && !previous_hash.empty() && *index_hash != previous_hash &&
      !preferred.empty() && preferred != "auto" && used_source != preferred) {
    safeErrorPrintLn(wpm_text(
        "command.wpm.warn.index_changed_nonpreferred",
        "wpm: warning: index content changed and was fetched from "
        "non-preferred source '{}' (preferred: '{}'); verify this mirror or "
        "re-run 'wpm index update' against the preferred source",
        used_source, preferred));
  }
  config["last_success_source"] = used_source;
  if (index_hash) config["last_index_sha256"] = *index_hash;
  save_config(opts.root, config);
  auto line = wpm_text("command.wpm.status.index_updated",
                       "wpm: index updated from {}", used_source);
  if (json_payload) {
    // `wpm index update --json` prints one machine-readable payload on
    // stdout; the human line moves to stderr (#1156). Auto-refreshes inside
    // other commands pass json_payload=false so they never leak a second
    // JSON document into the caller's stdout stream, while opts.json still
    // routes their human progress to stderr.
    safeErrorPrintLn(line);
    nlohmann::json payload = {{"schema", 1},
                              {"status", "updated"},
                              {"source", used_source},
                              {"index_sha256", index_hash.value_or("")}};
    safePrintLn(payload.dump(2));
    return 0;
  }
  if (opts.json) {
    safeErrorPrintLn(line);
    return 0;
  }
  safePrintLn(line);
  return 0;
}

// Compares locally installed packages against the remote index so users can
// see which packages have newer versions available upstream. Installed
// versions are anchored on install receipts (what wpm actually placed on
// disk), falling back to the local index for pre-receipt installs; available
// versions come from the freshly fetched remote index. Comparing the local
// index copy instead made `outdated` flip to "all up to date" after every
// `index update` even while old files were still installed (#1156).
auto package_is_installed(const fs::path& root, const nlohmann::json& pkg)
    -> bool;

auto list_outdated(const Options& opts) -> int {
  auto index = load_index(opts.root);
  std::string used_source;
  auto remote = try_fetch_index(opts, used_source);
  if (!remote) {
    safeErrorPrintLn(winux::i18n::translate(
        "command.wpm.error.outdated_unreachable",
        "wpm: unable to reach an index source; cannot check for updates"));
    return 1;
  }

  std::map<std::string, std::string> remote_versions;
  for (const auto& pkg : package_array(*remote)) {
    std::string name = pkg.value("name", "");
    if (!name.empty()) remote_versions[name] = pkg.value("version", "");
  }

  // Installed versions come from receipts first (what wpm actually placed on
  // disk), falling back to the local index for pre-receipt installs whose
  // destinations all exist. Comparing the local index copy against the remote
  // one made `outdated` agree with a just-updated index instead of the disk
  // and report "all up to date" while old files were still installed (#1156).
  std::map<std::string, std::string> installed_versions =
      receipt_install_records(opts.root);
  for (const auto& pkg : package_array(index)) {
    const std::string name = pkg.value("name", "");
    if (name.empty() || name == "winuxcmd") continue;
    if (installed_versions.contains(name)) continue;
    if (!package_is_installed(opts.root, pkg)) continue;
    installed_versions.emplace(name, pkg.value("version", ""));
  }

  int checked = 0;
  nlohmann::json outdated = nlohmann::json::array();
  for (const auto& [name, local_version] : installed_versions) {
    ++checked;
    auto it = remote_versions.find(name);
    if (it == remote_versions.end()) continue;
    if (it->second.empty() || it->second == local_version) continue;
    // A stale mirror can advertise an older version (or an older packaging
    // revision) than what is installed; only report an update when the remote
    // version actually orders higher. Vendor spellings keep the conservative
    // "difference means update" rule.
    if (auto order = compare_versions(it->second, local_version);
        order && *order <= 0)
      continue;
    outdated.push_back({{"name", name},
                        {"version", local_version},
                        {"available", it->second}});
    if (!opts.json)
      safePrintLn(wpm_text("command.wpm.status.outdated_available",
                           "wpm: update available: {} {} -> {}", name,
                           local_version, it->second));
  }

  if (opts.json) {
    nlohmann::json payload = {{"schema", 1},
                              {"source", used_source},
                              {"checked", checked},
                              {"outdated", std::move(outdated)}};
    safePrintLn(payload.dump(2));
    return 0;
  }
  if (outdated.empty())
    safePrintLn(
        winux::i18n::translate("command.wpm.status.outdated_none",
                               "wpm: all installed packages are up to date"));
  return 0;
}

auto list_links(const Options& opts) -> int {
  auto names = command_names();
  if (opts.json) {
    nlohmann::json commands = nlohmann::json::array();
    for (const auto& name : names) commands.push_back(name);
    nlohmann::json payload = {{"schema", 1},
                              {"bin", canonical_bin_dir(opts.root).string()},
                              {"count", names.size()},
                              {"commands", std::move(commands)}};
    safePrintLn(payload.dump(2));
    return 0;
  }
  for (const auto& name : names) safePrintLn(name);
  return 0;
}

auto print_index_status(const Options& opts) -> int {
  auto index = load_index(opts.root);
  auto packages = package_array(index);
  if (opts.json) {
    nlohmann::json payload = {
        {"schema", 1},
        {"root", opts.root.string()},
        {"local_index", local_index_path(opts.root).string()},
        {"fallback", "builtin"},
        {"version", index.value("version", "unknown")},
        {"updated", index.value("updated", "")},
        {"packages", packages.size()}};
    safePrintLn(payload.dump(2));
    return 0;
  }
  safePrintLn("WPM index");
  safePrintLn("  root: " + opts.root.string());
  safePrintLn("  local: " + local_index_path(opts.root).string());
  safePrintLn("  fallback: builtin");
  safePrintLn("  version: " + index.value("version", "unknown"));
  safePrintLn("  packages: " + std::to_string(packages.size()));
  return 0;
}

auto list_sources(const Options& opts) -> int {
  auto config = load_config(opts.root);
  auto preferred = config.value("preferred_source", "auto");
  auto sources = merged_sources(opts.root);

  if (opts.json) {
    nlohmann::json user_names = nlohmann::json::array();
    if (config.contains("user_sources") && config["user_sources"].is_array()) {
      for (const auto& item : config["user_sources"]) {
        if (item.is_object() && item.contains("name") &&
            item["name"].is_string())
          user_names.push_back(item["name"]);
      }
    }
    nlohmann::json payload = {{"schema", 1},
                              {"preferred", preferred},
                              {"region", config.value("region", "auto")},
                              {"sources", nlohmann::json::array()}};
    for (const auto& source : sources) {
      bool custom = false;
      for (const auto& name : user_names) {
        if (name == source.value("name", "")) custom = true;
      }
      payload["sources"].push_back(
          {{"name", source.value("name", "")},
           {"region", source.value("region", "")},
           {"priority", source.value("priority", 0)},
           {"description", source.value("description", "")},
           {"homepage", source.value("homepage", "")},
           {"index_urls",
            source.contains("index_urls") && source["index_urls"].is_array()
                ? source["index_urls"]
                : nlohmann::json::array()},
           {"custom", custom},
           {"preferred", preferred == source.value("name", "")}});
    }
    safePrintLn(payload.dump(2));
    return 0;
  }

  safePrintLn("WPM sources (preferred: " + preferred +
              ", region: " + config.value("region", "auto") + ")");
  for (const auto& source : sources) {
    std::string name = source.value("name", "");
    std::string region = source.value("region", "");
    size_t urls =
        source.contains("index_urls") && source["index_urls"].is_array()
            ? source["index_urls"].size()
            : 0;
    safePrintLn("  " + name + " region=" + region +
                " urls=" + std::to_string(urls));
    std::string description = source.value("description", "");
    if (!description.empty()) safePrintLn("    " + description);
    if (!opts.verbose) continue;
    std::string homepage = source.value("homepage", "");
    if (!homepage.empty()) safePrintLn("    homepage: " + homepage);
    if (source.contains("index_urls") && source["index_urls"].is_array()) {
      for (const auto& url : source["index_urls"]) {
        if (url.is_string()) safePrintLn("    url: " + url.get<std::string>());
      }
    }
  }
  return 0;
}

auto source_use(const Options& opts, std::string_view name) -> int {
  auto config = load_config(opts.root);
  config["preferred_source"] = std::string(name);
  if (!save_config(opts.root, config)) {
    safeErrorPrintLn("wpm: failed to save source preference");
    return 1;
  }
  safePrintLn("wpm: preferred source set to " + std::string(name));
  return 0;
}

auto source_region(const Options& opts, std::string_view region) -> int {
  if (region != "auto" && region != "global" && region != "cn") {
    safeErrorPrintLn(
        winux::i18n::translate("command.wpm.error.usage.region",
                               "wpm: usage: wpm source region auto|global|cn"));
    return 1;
  }
  auto config = load_config(opts.root);
  config["region"] = std::string(region);
  if (!save_config(opts.root, config)) {
    safeErrorPrintLn(winux::i18n::translate("command.wpm.error.save_region",
                                            "wpm: failed to save region"));
    return 1;
  }
  safePrintLn(wpm_text("command.wpm.status.region_set", "wpm: region set to {}",
                       std::string(region)));
  return 0;
}

auto source_add(const Options& opts, std::string_view name,
                std::string_view url) -> int {
  auto config = load_config(opts.root);
  if (!config.contains("user_sources") || !config["user_sources"].is_array()) {
    config["user_sources"] = nlohmann::json::array();
  }
  config["user_sources"].push_back(nlohmann::json{
      {"name", std::string(name)},
      {"region", "custom"},
      {"priority", 5},
      {"index_urls", nlohmann::json::array({std::string(url)})}});
  if (!save_config(opts.root, config)) {
    safeErrorPrintLn("wpm: failed to save source");
    return 1;
  }
  safePrintLn("wpm: source added: " + std::string(name));
  return 0;
}

// [wpm] remove a user-added source by name.  Built-in sources are
// protected: they are not stored in user_sources and cannot be removed.
auto source_remove(const Options& opts, std::string_view name) -> int {
  auto config = load_config(opts.root);
  if (!config.contains("user_sources") || !config["user_sources"].is_array()) {
    safeErrorPrintLn("wpm: source not found: " + std::string(name));
    return 1;
  }
  auto& sources = config["user_sources"];
  for (auto it = sources.begin(); it != sources.end(); ++it) {
    if (it->is_object() && it->value("name", "") == name) {
      sources.erase(it);
      if (!save_config(opts.root, config)) {
        safeErrorPrintLn("wpm: failed to save source");
        return 1;
      }
      // A removed source can no longer be the active one.
      if (config.contains("preferred_source") &&
          config["preferred_source"].is_string() &&
          config["preferred_source"].get<std::string>() == name) {
        config.erase("preferred_source");
        if (!save_config(opts.root, config)) {
          safeErrorPrintLn("wpm: failed to save source");
          return 1;
        }
      }
      safePrintLn("wpm: source removed: " + std::string(name));
      return 0;
    }
  }
  safeErrorPrintLn("wpm: source not found: " + std::string(name));
  return 1;
}

auto print_package_summary(const nlohmann::json& pkg) -> void {
  std::string version = pkg.value("version", "");
  if (!version.empty()) version = " " + version;
  std::string commands = join_json_string_array(pkg, "commands");
  if (!commands.empty()) commands = " [" + commands + "]";
  std::string category = pkg.value("category", "");
  if (!category.empty()) category = " {" + category + "}";
  safePrintLn("  [" + package_state_label(pkg) + "] " + pkg.value("name", "") +
              version + commands + category + " - " +
              pkg.value("description", ""));
}

auto package_is_installed(const fs::path& root, const nlohmann::json& pkg)
    -> bool {
  if (artifact_install_state(pkg) != "ready") return false;
  auto artifact = artifact_for_current_arch(pkg);
  if (!artifact) return false;
  auto destinations =
      artifact_destination_paths(root, *artifact, pkg.value("name", ""));
  if (!destinations || destinations->empty()) return false;

  for (const auto& dest : *destinations) {
    std::error_code ec;
    if (!fs::exists(dest, ec)) return false;
  }
  return true;
}

auto package_commands_json(const nlohmann::json& pkg) -> nlohmann::json {
  if (pkg.contains("commands") && pkg["commands"].is_array()) {
    return pkg["commands"];
  }
  return nlohmann::json::array();
}

auto package_summary_json(const fs::path& root, const nlohmann::json& pkg)
    -> nlohmann::json {
  nlohmann::json out = {{"name", pkg.value("name", "")},
                        {"version", pkg.value("version", "")},
                        {"kind", pkg.value("kind", "")},
                        {"category", pkg.value("category", "")},
                        {"license", pkg.value("license", "")},
                        {"commands", package_commands_json(pkg)},
                        {"description", pkg.value("description", "")},
                        {"state", package_state_label(pkg)},
                        {"install_state", artifact_install_state(pkg)},
                        {"installed", package_is_installed(root, pkg)}};

  if (auto artifact = artifact_for_current_arch(pkg)) {
    nlohmann::json artifact_json = {
        {"arch", detect_arch_key()},
        {"type", artifact->value("type", "")},
        {"layout", artifact_layout(*artifact)},
        {"runtime",
         artifact->value("runtime", pkg.value("runtime", std::string{}))},
        {"url_count", artifact_urls(*artifact).size()},
        {"sha256_present", !artifact->value("sha256", "").empty()},
        {"file_count",
         artifact->contains("files") && (*artifact)["files"].is_array()
             ? (*artifact)["files"].size()
             : 0}};
    if (auto size = artifact_size_bytes(*artifact))
      artifact_json["size"] = *size;
    out["artifact"] = std::move(artifact_json);
  } else {
    out["artifact"] = nullptr;
  }
  return out;
}

auto package_info_json(const fs::path& root, const nlohmann::json& pkg)
    -> nlohmann::json {
  auto out = pkg;
  out["wpm"] = package_summary_json(root, pkg);
  return out;
}

auto print_installed_package_summary(const nlohmann::json& pkg) -> void {
  std::string version = pkg.value("version", "");
  if (!version.empty()) version = " " + version;
  std::string commands = join_json_string_array(pkg, "commands");
  if (!commands.empty()) commands = " [" + commands + "]";
  std::string category = pkg.value("category", "");
  if (!category.empty()) category = " {" + category + "}";
  safePrintLn("  [installed] " + pkg.value("name", "") + version + commands +
              category + " - " + pkg.value("description", ""));
}

auto list_installed_packages(const Options& opts) -> int {
  auto index = load_index(opts.root);
  int matched = 0;
  nlohmann::json json_packages = nlohmann::json::array();
  for (const auto& pkg : package_array(index)) {
    if (!package_is_installed(opts.root, pkg)) continue;
    if (opts.json) {
      json_packages.push_back(package_summary_json(opts.root, pkg));
    } else {
      print_installed_package_summary(pkg);
    }
    ++matched;
  }

  if (opts.json) {
    nlohmann::json payload = {{"schema", 1},
                              {"matched", matched},
                              {"packages", std::move(json_packages)}};
    safePrintLn(payload.dump(2));
    return 0;
  }

  if (matched == 0) {
    safePrintLn("wpm: no installed packages matched the local index");
    safePrintLn(
        "wpm: run 'wpm index update' if the index is missing packages you "
        "installed");
  }
  return 0;
}

auto export_installed_packages_plain(const Options& opts) -> int {
  auto index = load_index(opts.root);
  for (const auto& pkg : package_array(index)) {
    const std::string name = pkg.value("name", "");
    if (name.empty() || name == "winuxcmd") continue;
    if (!package_is_installed(opts.root, pkg)) continue;
    safePrintLn(name);
  }
  return 0;
}

// Uninstall removes the files a package's artifact mapping placed on disk.
// Installation state is derived (no manifest), so removal targets are exactly
// the destinations `package_is_installed` checks. Protected files — the
// canonical winuxcmd.exe and the running executable — are never deleted.
auto uninstall_remove_destination(const Options& opts, const fs::path& dest,
                                  bool directory,
                                  std::vector<std::string>& removed,
                                  std::vector<std::string>& errors) -> bool {
  const fs::path core = canonical_winuxcmd_path(opts.root);
  std::error_code ec;
  if (same_file(core, dest) || same_file(current_exe_path(), dest)) {
    errors.push_back(wpm_text("command.wpm.error.uninstall_protected",
                              "wpm: skipping protected file '{}'",
                              dest.string()));
    return false;
  }
  if (!fs::exists(dest, ec)) return true;
  if (directory) {
    // Trailing separators make filename() empty; fall back to the last
    // non-empty component so the progress label names the package folder.
    std::string kind = dest.filename().string();
    if (kind.empty()) kind = dest.parent_path().filename().string();
    if (kind.empty()) kind = "package files";
    const auto stats = remove_tree(dest, kind, opts.verbose, !opts.json,
                                   "command.wpm.status.uninstall_progress");
    if (stats.ec) {
      errors.push_back(wpm_text("command.wpm.error.uninstall_failed",
                                "wpm: failed to remove '{}': {}", dest.string(),
                                stats.ec.message()));
      return false;
    }
  } else {
    fs::remove(dest, ec);
    if (ec) {
      errors.push_back(wpm_text("command.wpm.error.uninstall_failed",
                                "wpm: failed to remove '{}': {}", dest.string(),
                                ec.message()));
      return false;
    }
  }
  removed.push_back(dest.string());
  // Prune now-empty parent directories up to usr/bin (never above root).
  fs::path parent = dest.parent_path();
  const fs::path bin_dir = canonical_bin_dir(opts.root);
  while (parent != bin_dir && parent != opts.root && !parent.empty()) {
    std::error_code empty_ec;
    if (!fs::is_empty(parent, empty_ec) || empty_ec) break;
    std::error_code rm_ec;
    fs::remove(parent, rm_ec);
    if (rm_ec) break;
    parent = parent.parent_path();
  }
  return true;
}

auto uninstall_package(const Options& opts, const nlohmann::json& pkg)
    -> nlohmann::json {
  const std::string name = pkg.value("name", "");
  std::vector<std::string> removed;
  std::vector<std::string> errors;
  bool ok = true;

  if (name == "winuxcmd") {
    errors.push_back(
        wpm_text("command.wpm.error.uninstall_core",
                 "wpm: refusing to uninstall '{}': use 'wpm update winuxcmd' "
                 "to manage the core instead",
                 name));
    return {{"name", name},
            {"status", "refused"},
            {"removed", removed},
            {"errors", errors},
            {"dry_run", opts.dry_run}};
  }

  auto artifact = artifact_for_current_arch(pkg);
  if (!artifact || artifact_install_state(pkg) != "ready") {
    return {{"name", name},
            {"status", "not_installed"},
            {"removed", removed},
            {"errors", errors},
            {"dry_run", opts.dry_run}};
  }
  auto destinations = artifact_destination_paths(opts.root, *artifact, name);
  if (!destinations || destinations->empty()) {
    return {{"name", name},
            {"status", "not_installed"},
            {"removed", removed},
            {"errors", errors},
            {"dry_run", opts.dry_run}};
  }

  for (const auto& mapping : (*artifact)["files"]) {
    bool directory = mapping_is_directory(mapping);
    auto dest =
        artifact_destination_for_mapping(opts.root, *artifact, name, mapping);
    if (!dest) continue;
    if (opts.dry_run) {
      std::error_code ec;
      if (fs::exists(*dest, ec)) removed.push_back(dest->string());
      continue;
    }
    if (!uninstall_remove_destination(opts, *dest, directory, removed,
                                      errors)) {
      ok = false;
    }
  }
  // Entry links live in <root>/bin on the host PATH, not under the payload, so
  // they are derived from the package's commands rather than the file mapping.
  for (const auto& [payload, link] : package_entry_links(opts.root, pkg,
                                                         *artifact)) {
    (void)payload;
    if (opts.dry_run) {
      std::error_code ec;
      if (fs::exists(link, ec)) removed.push_back(link.string());
      continue;
    }
    if (!uninstall_remove_destination(opts, link, false, removed, errors)) {
      ok = false;
    }
  }

  // Shim-layout packages keep their payload under opt\<pkg> and forwarders in
  // usr\bin; drop the forwarders only when they are ours. "Ours" is either a
  // link to the small forwarder (`winuxcmd-shim.exe`, the current model) or a
  // link/copy of winuxcmd.exe itself (an installation that predates the
  // forwarder) - anything else is a user file and stays.
  if (artifact_layout(*artifact) == "shim" && !opts.dry_run) {
    const fs::path self_exe = canonical_winuxcmd_path(opts.root);
    const fs::path forwarder = package_shim_source(opts.root);
    auto is_our_shim = [&](const fs::path& candidate) {
      if (!forwarder.empty() && same_file(forwarder, candidate)) return true;
      return same_file(self_exe, candidate) || files_equal(self_exe, candidate);
    };
    std::vector<std::string> shim_commands;
    for (const auto& item : package_commands_json(pkg)) {
      if (item.is_string()) shim_commands.push_back(item.get<std::string>());
    }
    // Aliases first: an alias is a link of its command's shim, so the identity
    // check has to run while that shim is still in place.
    if (pkg.contains("aliases") && pkg["aliases"].is_array()) {
      for (const auto& alias : pkg["aliases"]) {
        const auto alias_name = alias.is_string() ? alias.get<std::string>()
                                                  : alias.value("name", "");
        auto target_name =
            alias.is_string() ? std::string{} : alias.value("target", "");
        if (target_name.empty() && !shim_commands.empty())
          target_name = shim_commands[0];
        if (alias_name.empty() || target_name.empty()) continue;
        const auto command_shim =
            canonical_bin_dir(opts.root) / exe_suffix(target_name);
        const auto alias_path =
            canonical_bin_dir(opts.root) / exe_suffix(alias_name);
        std::error_code ec;
        if (!fs::exists(alias_path, ec)) continue;
        if (same_file(command_shim, alias_path)) {
          if (!uninstall_remove_destination(opts, alias_path, false, removed,
                                            errors))
            ok = false;
        } else {
          errors.push_back(wpm_text("command.wpm.error.uninstall_protected",
                                    "wpm: skipping protected file '{}'",
                                    alias_path.string()));
        }
      }
    }
    for (const auto& command : shim_commands) {
      fs::path shim = canonical_bin_dir(opts.root) / exe_suffix(command);
      std::error_code ec;
      if (!fs::exists(shim, ec)) continue;
      if (is_our_shim(shim)) {
        if (!uninstall_remove_destination(opts, shim, false, removed, errors)) {
          ok = false;
        }
      } else {
        errors.push_back(wpm_text("command.wpm.error.uninstall_protected",
                                  "wpm: skipping protected file '{}'",
                                  shim.string()));
      }
    }
    // A map entry that names a payload this uninstall just removed would keep
    // `links rebuild` looking for a command that no longer exists.
    remove_shim_map_entries(opts.root, shim_commands);
    fs::path payload = package_payload_dir(opts.root, name);
    std::error_code payload_ec;
    if (fs::exists(payload, payload_ec)) {
      fs::remove_all(payload, payload_ec);
      if (payload_ec) {
        errors.push_back(wpm_text("command.wpm.error.uninstall_failed",
                                  "wpm: failed to remove '{}': {}",
                                  payload.string(), payload_ec.message()));
        ok = false;
      } else {
        removed.push_back(payload.string());
      }
    }
  }

  const std::string status =
      ok ? (opts.dry_run ? "would_remove" : "removed") : "error";
  if (ok && !opts.dry_run) {
    remove_install_receipt(opts.root, name);
  }
  return {{"name", name},
          {"status", status},
          {"removed", removed},
          {"errors", errors},
          {"dry_run", opts.dry_run}};
}

auto uninstall_packages(const Options& opts,
                        std::span<const std::string_view> names) -> int {
  auto index = load_index(opts.root);
  nlohmann::json results = nlohmann::json::array();
  int failed = 0;
  int removed_count = 0;

  // Interactive confirmation. Skipped for --yes, --dry-run, and --json
  // (non-interactive consumers). Only packages that are actually installed
  // and removable are listed.
  if (!opts.yes && !opts.dry_run && !opts.json) {
    std::vector<std::string> targets;
    for (const auto& name_view : names) {
      const std::string name(name_view);
      auto pkg = find_package(index, name);
      if (!pkg || name == "winuxcmd") continue;
      auto artifact = artifact_for_current_arch(*pkg);
      if (!artifact || artifact_install_state(*pkg) != "ready") continue;
      auto destinations =
          artifact_destination_paths(opts.root, *artifact, name);
      if (!destinations || destinations->empty()) continue;
      targets.push_back(name);
    }
    if (!targets.empty()) {
      std::string joined;
      for (size_t i = 0; i < targets.size(); ++i) {
        if (i > 0) joined += ", ";
        joined += targets[i];
      }
      safePrintLn(wpm_text("command.wpm.status.uninstall_confirm",
                           "wpm: the following packages will be removed: {}",
                           joined));
      safePrint(wpm_text("command.wpm.status.uninstall_prompt",
                         "wpm: continue? [y/N] "));
      std::string line;
      std::getline(std::cin, line);
      line = lower_ascii(trim_ascii(std::move(line)));
      if (line != "y" && line != "yes") {
        safePrintLn(wpm_text("command.wpm.status.uninstall_aborted",
                             "wpm: aborted; nothing was removed"));
        return 0;
      }
    }
  }

  for (const auto& name_view : names) {
    const std::string name(name_view);
    auto pkg = find_package(index, name);
    if (!pkg) {
      results.push_back({{"name", name},
                         {"status", "not_found"},
                         {"removed", nlohmann::json::array()},
                         {"errors", nlohmann::json::array()},
                         {"dry_run", opts.dry_run}});
      safeErrorPrintLn(wpm_text("command.wpm.error.uninstall_not_found",
                                "wpm: package not found in index: {}", name));
      ++failed;
      continue;
    }

    nlohmann::json result = uninstall_package(opts, *pkg);
    const std::string& status = result["status"].get_ref<const std::string&>();
    if (status == "not_installed") {
      safeErrorPrintLn(wpm_text("command.wpm.error.uninstall_not_installed",
                                "wpm: package '{}' is not installed", name));
    } else if (status == "refused" || status == "error") {
      ++failed;
      for (const auto& err : result["errors"])
        safeErrorPrintLn(err.get<std::string>());
    } else {
      ++removed_count;
      // Keep stdout pure JSON in --json mode; human progress goes to stderr.
      auto line = status == "would_remove"
                      ? wpm_text("command.wpm.status.uninstall_dry_run",
                                 "wpm: dry-run: would remove {}", name)
                      : wpm_text("command.wpm.status.uninstall_removed",
                                 "wpm: uninstalled {}", name);
      if (opts.json) {
        safeErrorPrintLn(line);
      } else {
        safePrintLn(line);
        if (opts.verbose) {
          for (const auto& path : result["removed"])
            safePrintLn("  - " + path.get<std::string>());
        }
      }
    }
    results.push_back(std::move(result));
  }

  if (opts.json) {
    nlohmann::json payload = {
        {"schema", 1}, {"results", std::move(results)}, {"failed", failed}};
    safePrintLn(payload.dump(2));
    return failed == 0 ? 0 : 1;
  }

  safePrintLn(wpm_text("command.wpm.status.uninstall_summary",
                       "wpm: uninstall complete removed={} failed={}",
                       removed_count, failed));
  return failed == 0 ? 0 : 1;
}

auto package_list_entry(std::string line) -> std::optional<std::string> {
  line = trim_ascii(std::move(line));
  if (line.empty() || line.starts_with('#')) return std::nullopt;
  return line;
}

auto restore_packages(const Options& opts, const fs::path& package_list)
    -> int {
  auto text = read_text(package_list);
  if (!text) {
    safeErrorPrintLn(wpm_text("command.wpm.error.read_package_list",
                              "wpm: failed to read package list: {}",
                              package_list.string()));
    return 1;
  }

  std::vector<std::string> packages;
  std::istringstream input(*text);
  std::string line;
  while (std::getline(input, line)) {
    if (auto entry = package_list_entry(std::move(line))) {
      packages.push_back(std::move(*entry));
    }
  }

  if (packages.empty()) {
    safePrintLn(wpm_text("command.wpm.status.restore_empty",
                         "wpm: package list is empty: {}",
                         package_list.string()));
    return 0;
  }

  std::vector<std::string_view> package_views;
  package_views.reserve(packages.size());
  for (const auto& package : packages) package_views.push_back(package);
  return install_packages(opts, package_views);
}

auto list_packages(const Options& opts, std::string_view query = {}) -> int {
  auto index = load_index(opts.root);
  int matched = 0;
  int hidden = 0;
  bool ready_only = query.empty() && !opts.all;
  nlohmann::json json_packages = nlohmann::json::array();

  for (const auto& pkg : package_array(index)) {
    if (!package_matches_query(pkg, query)) continue;
    if (!package_matches_category(pkg, opts.category)) continue;
    if (ready_only && package_state_label(pkg) != "ready") {
      ++hidden;
      continue;
    }
    if (opts.json) {
      json_packages.push_back(package_summary_json(opts.root, pkg));
    } else {
      print_package_summary(pkg);
    }
    ++matched;
  }

  if (opts.json) {
    nlohmann::json payload = {{"schema", 1},
                              {"query", std::string(query)},
                              {"category", opts.category},
                              {"all", opts.all},
                              {"matched", matched},
                              {"hidden", hidden},
                              {"packages", std::move(json_packages)}};
    safePrintLn(payload.dump(2));
    return 0;
  }

  if (!query.empty() && matched == 0) {
    safePrintLn("wpm: no packages matched '" + std::string(query) + "'");
    safePrintLn(
        "wpm: run 'wpm index update' to refresh the independent "
        "wpm-source index");
  } else if (!opts.category.empty() && matched == 0) {
    safePrintLn("wpm: no packages matched category '" + opts.category + "'");
  } else if (query.empty() && matched == 0) {
    safePrintLn("wpm: no packages in the local index");
    safePrintLn(
        "wpm: run 'wpm index update' to fetch the independent "
        "wpm-source index");
  } else if (ready_only && hidden > 0) {
    safePrintLn("wpm: hidden " + std::to_string(hidden) +
                " index-only packages; use --all to show placeholders");
  }
  return 0;
}

auto list_categories(const Options& opts) -> int {
  auto index = load_index(opts.root);
  struct Counts {
    int packages = 0;
    int ready = 0;
    int index_only = 0;
    int installed = 0;
  };
  std::map<std::string, Counts> categories;

  for (const auto& pkg : package_array(index)) {
    std::string category = pkg.value("category", "");
    if (category.empty()) category = "uncategorized";
    auto& counts = categories[category];
    ++counts.packages;
    if (package_state_label(pkg) == "ready") {
      ++counts.ready;
    } else {
      ++counts.index_only;
    }
    if (package_is_installed(opts.root, pkg)) ++counts.installed;
  }

  if (opts.json) {
    nlohmann::json payload = {{"schema", 1},
                              {"categories", nlohmann::json::array()},
                              {"total", categories.size()}};
    for (const auto& [name, counts] : categories) {
      payload["categories"].push_back({{"name", name},
                                       {"packages", counts.packages},
                                       {"ready", counts.ready},
                                       {"index_only", counts.index_only},
                                       {"installed", counts.installed}});
    }
    safePrintLn(payload.dump(2));
    return 0;
  }

  for (const auto& [name, counts] : categories) {
    safePrintLn("  " + name + " packages=" + std::to_string(counts.packages) +
                " ready=" + std::to_string(counts.ready) +
                " index-only=" + std::to_string(counts.index_only) +
                " installed=" + std::to_string(counts.installed));
  }
  return 0;
}

auto show_info(const Options& opts, std::string_view name) -> int {
  auto index = load_index(opts.root);
  auto pkg = find_package(index, name);
  if (!pkg && !opts.json) {
    if (refresh_index_once(opts, "package not found in local index")) {
      index = load_index(opts.root);
      pkg = find_package(index, name);
    }
  }
  if (!pkg) {
    if (opts.json) {
      nlohmann::json payload = {{"schema", 1},
                                {"error", "package_not_found"},
                                {"package", std::string(name)}};
      safePrintLn(payload.dump(2));
    } else {
      safeErrorPrintLn("wpm: package not found after index update: " +
                       std::string(name));
    }
    return 1;
  }
  if (opts.json) {
    auto payload = package_info_json(opts.root, *pkg);
    payload["schema"] = 1;
    if (const auto target = package_alias_target(*pkg); !target.empty())
      payload["alias_of"] = target;
    safePrintLn(payload.dump(2));
    return 0;
  }
  safePrintLn("Name: " + pkg->value("name", ""));
  safePrintLn("Version: " + pkg->value("version", ""));
  if (const auto target = package_alias_target(*pkg); !target.empty())
    safePrintLn("Alias of: " + target);
  safePrintLn("Kind: " + pkg->value("kind", ""));
  safePrintLn("Category: " + pkg->value("category", ""));
  safePrintLn("License: " + pkg->value("license", ""));
  safePrintLn("Commands: " + join_json_string_array(*pkg, "commands"));
  safePrintLn("Description: " + pkg->value("description", ""));
  safePrintLn("Install state: " + artifact_install_state(*pkg));
  auto artifact = artifact_for_current_arch(*pkg);
  safePrintLn("Artifact: " +
              std::string(artifact ? detect_arch_key() : "none"));
  if (artifact) {
    safePrintLn("Type: " + artifact->value("type", ""));
    const std::string runtime =
        artifact->value("runtime", pkg->value("runtime", std::string{}));
    if (!runtime.empty()) {
      safePrintLn("Runtime: " + runtime);
    }
    safePrintLn("URLs: " + std::to_string(artifact_urls(*artifact).size()));
    if (auto size = artifact_size_bytes(*artifact)) {
      safePrintLn("Size: " + human_size(*size));
    }
    std::string sha256 = artifact->value("sha256", "");
    safePrintLn("SHA256: " +
                std::string(sha256.empty() ? "missing" : "present"));
    size_t file_count =
        artifact->contains("files") && (*artifact)["files"].is_array()
            ? (*artifact)["files"].size()
            : 0;
    safePrintLn("Files: " + std::to_string(file_count));
  }
  return 0;
}

auto print_usage() -> int {
  const std::string help =
      "Winux Package Manager {}\n"
      "Usage: wpm <command> [args] [options]\n"
      "\n"
      "Commands:\n"
      "  links list|rebuild|remove             manage WinuxCmd hardlinks\n"
      "  clean [cache|staging|all]             remove transient downloads and "
      "staging\n"
      "  cache clean [cache|staging|all]       alias for clean\n"
      "  index status|update                   inspect or refresh local index\n"
      "  update-index                          alias for index update\n"
      "  source list|use|add|region|test       manage and test index sources\n"
      "  list                                  list indexed packages and "
      "install state\n"
      "  categories                            list package categories and "
      "counts\n"
      "  search <query>                        search names, commands, "
      "categories, licenses\n"
      "  info <package>                        show package metadata\n"
      "  install <package>...                  install one or more packages; "
      "use\n"
      "                                        --force to reinstall or "
      "overwrite\n"
      "  installed                             list packages present in this "
      "root\n"
      "  export [--plain]                      print installed package names "
      "for profiles\n"
      "  restore <file>                        install packages from a plain "
      "list\n"
      "  outdated                              list installed packages with "
      "updates\n"
      "  uninstall|remove|erase <package>...   uninstall one or more packages\n"
      "  update|upgrade winuxcmd               update WinuxCmd from local "
      "index\n"
      "\n"
      "Options:\n"
      "  -r, --root <dir>                      manage a specific WinuxCmd "
      "root\n"
      "  -s, --source <name>                   use a specific index source\n"
      "  -a, --all                             show index-only packages in "
      "list "
      "output\n"
      "  -f, --force                           overwrite existing files when "
      "safe\n"
      "  -n, --dry-run                         show planned changes without "
      "writing\n"
      "  -v, --verbose                         print detailed progress\n"
      "  -y, --yes                             assume yes for interactive "
      "prompts\n"
      "      --category <name>                 filter list/search output by "
      "category\n"
      "      --json                            print machine-readable JSON\n"
      "      --plain                           print only package names for "
      "export\n"
      "      --proxy <url>                     use the given HTTP proxy for "
      "downloads (env: HTTP_PROXY,\n"
      "                                        HTTPS_PROXY, WPM_HTTP_PROXY, "
      "WPM_HTTPS_PROXY, NO_PROXY)\n"
      "      --from <manifest>                 install packages from a "
      "manifest "
      "file or URL\n"
      "                                        (TOML, JSON, or plain list)\n"
      "      --help                            display this help and exit\n"
      "  -V, --version                         output version information and "
      "exit\n";
  safePrint(cmd::meta::format_custom_help(
      "wpm", wpm_text("command.wpm.custom_help", help, kVersion)));
  return 0;
}

auto build_options(const CommandContext<WPM_OPTIONS.size()>& ctx) -> Options {
  Options opts;
  std::string root = ctx.get<std::string>("--root", "");
  if (root.empty()) root = ctx.get<std::string>("-r", "");
  opts.root = root.empty() ? default_root() : fs::path(root);
  opts.source = ctx.get<std::string>("--source", "");
  if (opts.source.empty()) opts.source = ctx.get<std::string>("-s", "");
  opts.all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  opts.force = ctx.get<bool>("--force", false) || ctx.get<bool>("-f", false);
  opts.dry_run =
      ctx.get<bool>("--dry-run", false) || ctx.get<bool>("-n", false);
  opts.verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  opts.yes = ctx.get<bool>("--yes", false) || ctx.get<bool>("-y", false);
  opts.category = ctx.get<std::string>("--category", "");
  opts.proxy = ctx.get<std::string>("--proxy", "");
  opts.from = ctx.get<std::string>("--from", "");
  opts.json = ctx.get<bool>("--json", false);
  opts.plain = ctx.has("--plain");
  if (!opts.dry_run) (void)ensure_install_layout(opts.root);
  return opts;
}

auto dispatch(const Options& opts, std::span<const std::string_view> args)
    -> int {
  if (args.empty()) return print_usage();

  if (!opts.proxy.empty() && !user_forced_proxy(opts)) {
    safeErrorPrintLn(wpm_text("command.wpm.error.invalid_proxy",
                              "wpm: invalid proxy '{}': use http://host:port",
                              opts.proxy));
    return 1;
  }

  if (args[0] == "__apply-update") {
    return apply_update(args.subspan(1));
  }

  if (args[0] == "links") {
    if (args.size() == 1 || args[1] == "list") return list_links(opts);
    if (args[1] == "rebuild") {
      return rebuild_links(opts.root, opts.force, opts.dry_run, opts.verbose);
    }
    if (args[1] == "remove") {
      return remove_links(opts.root, opts.dry_run);
    }
    if (args[1] == "verify") return verify_links(opts);
    safeErrorPrintLn(
        winux::i18n::translate("command.wpm.error.usage.links",
                               "wpm: usage: wpm links list|rebuild|remove|verify"));
    return 1;
  }

  if (args[0] == "clean") {
    return clean_state(opts, args.size() >= 2 ? args[1] : std::string_view{});
  }
  if (args[0] == "cache") {
    if (args.size() >= 2 && args[1] == "clean")
      return clean_state(opts, args.size() >= 3 ? args[2] : std::string_view{});
    safeErrorPrintLn(winux::i18n::translate(
        "command.wpm.error.usage.cache",
        "wpm: usage: wpm cache clean [cache|staging|all]"));
    return 1;
  }

  if (args[0] == "index" || args[0] == "update-index") {
    if (args[0] == "update-index" || (args.size() >= 2 && args[1] == "update"))
      return update_index(opts, opts.json);
    if (args.size() == 1 || args[1] == "status")
      return print_index_status(opts);
    safeErrorPrintLn(
        winux::i18n::translate("command.wpm.error.usage.index",
                               "wpm: usage: wpm index status|update"));
    return 1;
  }

  if (args[0] == "source") {
    if (args.size() == 1 || args[1] == "list") return list_sources(opts);
    if (args[1] == "use" && args.size() >= 3) return source_use(opts, args[2]);
    if (args[1] == "add" && args.size() >= 4)
      return source_add(opts, args[2], args[3]);
    if ((args[1] == "remove" || args[1] == "delete") && args.size() >= 3)
      return source_remove(opts, args[2]);
    if (args[1] == "region" && args.size() >= 3)
      return source_region(opts, args[2]);
    if (args[1] == "test") return update_index(opts, opts.json);
    safeErrorPrintLn(winux::i18n::translate(
        "command.wpm.error.usage.source",
        "wpm: usage: wpm source list|use <name>|add <name> <url>|"
        "remove <name>|region <auto|global|cn>|test"));
    return 1;
  }

  if (args[0] == "list") return list_packages(opts);
  if (args[0] == "categories") return list_categories(opts);
  if (args[0] == "installed") return list_installed_packages(opts);
  if (args[0] == "outdated") return list_outdated(opts);
  if (args[0] == "uninstall" || args[0] == "remove" || args[0] == "erase") {
    if (args.size() >= 2) {
      std::vector<std::string_view> packages;
      packages.reserve(args.size() - 1);
      for (size_t i = 1; i < args.size(); ++i) packages.push_back(args[i]);
      return uninstall_packages(opts, packages);
    }
    safeErrorPrintLn(winux::i18n::translate(
        "command.wpm.error.usage.uninstall",
        "wpm: usage: wpm uninstall <package>... [--dry-run]"));
    return 1;
  }
  if (args[0] == "export") {
    if (args.size() == 1 || opts.plain ||
        (args.size() == 2 && args[1] == "--plain")) {
      return export_installed_packages_plain(opts);
    }
    safeErrorPrintLn(winux::i18n::translate(
        "command.wpm.error.usage.export", "wpm: usage: wpm export [--plain]"));
    return 1;
  }
  if (args[0] == "restore") {
    if (args.size() == 2)
      return restore_packages(opts, fs::path(std::string(args[1])));
    safeErrorPrintLn(winux::i18n::translate("command.wpm.error.usage.restore",
                                            "wpm: usage: wpm restore <file>"));
    return 1;
  }
  if (args[0] == "search")
    return list_packages(opts, args.size() >= 2 ? args[1] : std::string_view{});
  if (args[0] == "info") {
    if (args.size() >= 2) return show_info(opts, args[1]);
    safeErrorPrintLn(winux::i18n::translate("command.wpm.error.usage.info",
                                            "wpm: usage: wpm info <package>"));
    return 1;
  }
  if (args[0] == "install") {
    std::vector<std::string_view> packages;
    packages.reserve(args.size() - 1);
    for (size_t i = 1; i < args.size(); ++i) packages.push_back(args[i]);
    if (!opts.from.empty()) {
      if (!packages.empty()) {
        safeErrorPrintLn(winux::i18n::translate(
            "command.wpm.error.manifest_conflict",
            "wpm: --from cannot be combined with package arguments"));
        return 1;
      }
      return install_from_manifest(opts, opts.from);
    }
    if (!packages.empty()) return install_packages(opts, packages);
    safeErrorPrintLn(
        winux::i18n::translate("command.wpm.error.usage.install",
                               "wpm: usage: wpm install <package>... "
                               "[--from <manifest>]"));
    return 1;
  }
  if (args[0] == "update" || args[0] == "upgrade") {
    if (args.size() >= 2 && (args[1] == "winuxcmd" || args[1] == "coreutils")) {
      return update_winuxcmd(opts);
    }
    safeErrorPrintLn(
        winux::i18n::translate("command.wpm.error.usage.update",
                               "wpm: usage: wpm update|upgrade winuxcmd"));
    return 1;
  }
  if (args[0] == "version") {
    safePrintLn(wpm_text("command.wpm.version", "wpm {}", kVersion));
    return 0;
  }

  safeErrorPrintLn(wpm_text("command.wpm.error.unknown_command",
                            "wpm: unknown command: {}", args[0]));
  return 1;
}

}  // namespace wpm

REGISTER_COMMAND(
    wpm, "wpm", "manage WinuxCmd packages and command links",
    "WPM is WinuxCmd's internal package and link manager. It keeps a local "
    "package index, downloads artifacts with WinHTTP, updates WinuxCmd, cleans "
    "transient state, and rebuilds command hardlinks without external scripts.",
    "  wpm links rebuild\n"
    "  wpm clean\n"
    "  wpm index update\n"
    "  wpm source list\n"
    "  wpm source region auto|global|cn\n"
    "  wpm outdated\n"
    "  wpm uninstall <package>\n"
    "  wpm update winuxcmd",
    "winuxcmd(1), ln(1)", "WinuxCmd Project", "Copyright (c) 2026 WinuxCmd",
    WPM_OPTIONS) {
  try {
    auto opts = wpm::build_options(ctx);
    return wpm::dispatch(
        opts, std::span<const std::string_view>(ctx.positionals.data(),
                                                ctx.positionals.size()));
  } catch (const std::exception& e) {
    safeErrorPrintLn(winux::i18n::format("command.wpm.error.exception",
                                         "wpm: {}", e.what()));
    return 1;
  }
}
