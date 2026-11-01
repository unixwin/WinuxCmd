// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for namei (display path resolution
///     steps), util-linux 2.39.3 misc-utils/namei.c option set. The
///     base walk and its 'd'/'f'/'s' glyphs are the established
///     WinuxCmd dialect (pinned by the #1237 dialect tests); the
///     upstream options -m/-o/-v/-x/-n/-Z and the -l combination are
///     layered on it. The inert -a/--access and -d/--noreport
///     extensions were removed: they promised behavior that never
///     existed and have no upstream counterpart.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <winioctl.h>

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr NAMEI_OPTIONS = std::array{
    OPTION("-l", "--long", "long format (modes, owners, vertical)"),
    OPTION("-m", "--modes", "show file modes in rwx form"),
    OPTION("-n", "--nosymlinks", "do not follow symbolic links"),
    OPTION("-o", "--owners", "show owner and group of each component"),
    OPTION("-v", "--vertical", "show the path components vertically"),
    OPTION("-x", "--mountpoints", "show mount point directories with a 'D'"),
    OPTION("-Z", "--context", "print the security context")};

namespace namei_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool modes = false;
  bool owners = false;
  bool vertical = false;
  bool nosymlinks = false;
  bool mountpoints = false;
  bool context = false;
  SmallVector<std::string, 16> paths;
};

auto build_config(const CommandContext<NAMEI_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.modes = ctx.has("-m") || ctx.has("--modes");
  cfg.owners = ctx.has("-o") || ctx.has("--owners");
  cfg.vertical = ctx.has("-v") || ctx.has("--vertical");
  cfg.nosymlinks = ctx.has("-n") || ctx.has("--nosymlinks");
  cfg.mountpoints = ctx.has("-x") || ctx.has("--mountpoints");
  cfg.context = ctx.has("-Z") || ctx.has("--context");
  if (ctx.has("-l") || ctx.has("--long")) {
    // util-linux: -l is exactly -m -o -v combined.
    cfg.modes = true;
    cfg.owners = true;
    cfg.vertical = true;
  }
  for (auto arg : ctx.positionals) cfg.paths.push_back(std::string(arg));
  if (cfg.paths.empty()) return std::unexpected("missing operand");
  return cfg;
}

// One probed component of the walk.
struct StepInfo {
  std::wstring prefix;
  std::wstring name;
  bool exists = false;
  bool is_root = false;
  DWORD attrs = 0;
  bool is_symlink = false;  // IO_REPARSE_TAG_SYMLINK
  bool is_mount = false;    // root step or IO_REPARSE_TAG_MOUNT_POINT
  std::optional<std::string> target;
  Win32FileAccounts accounts;
};

auto probe_step(const std::wstring& prefix, bool want_owners) -> StepInfo {
  StepInfo info;
  info.prefix = prefix;
  const std::wstring& wpath = prefix;
  const DWORD attrs = GetFileAttributesW(wpath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return info;
  info.exists = true;
  info.attrs = attrs;

  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    HANDLE h = CreateFileW(
        wpath.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      // The reparse tag distinguishes symlinks from mount points
      // (junctions / volume mounts), which namei reports as mounts.
      std::vector<std::byte> buffer(16384);
      DWORD returned = 0;
      ULONG tag = 0;
      if (DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer.data(),
                          static_cast<DWORD>(buffer.size()), &returned,
                          nullptr) &&
          returned >= sizeof(ULONG)) {
        std::memcpy(&tag, buffer.data(), sizeof(tag));
      }
      if (tag == IO_REPARSE_TAG_MOUNT_POINT) {
        info.is_mount = true;
      } else {
        // Symlinks (and any other reparse flavor) keep the historical
        // 's ... -> target' rendering.
        info.is_symlink = true;
        constexpr size_t buf_size = 4096;
        std::vector<wchar_t> buf(buf_size);
        const DWORD ret = GetFinalPathNameByHandleW(
            h, buf.data(), static_cast<DWORD>(buf_size), VOLUME_NAME_DOS);
        if (ret > 0 && ret < buf_size) {
          info.target = wstring_to_utf8(buf.data());
        }
      }
      CloseHandle(h);
    } else {
      info.is_symlink = true;
    }
  }
  if (want_owners) info.accounts = win32_file_accounts(prefix);
  return info;
}

// One resolution step over the folded native spelling: the cumulative
// prefix to probe, and the component tail to display.
struct WalkStep {
  std::wstring prefix;
  std::wstring name;
};

// Split the folded native spelling into incremental prefixes.
// "E:\a\b" -> "E:\", "E:\a", "E:\a\b"; relative "a\b" -> "a", "a\b";
// UNC "\\srv\share\x" -> "\\srv\share", "\\srv\share\x"; a POSIX-rooted
// spelling that no fold claimed ("/usr/bin") roots at "/".
auto native_walk_steps(std::wstring_view native) -> std::vector<WalkStep> {
  const auto is_sep = [](wchar_t ch) { return ch == L'/' || ch == L'\\'; };
  const auto is_letter = [](wchar_t ch) {
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
  };
  std::vector<WalkStep> steps;
  if (native.empty()) return steps;

  size_t pos = 0;
  if (native.size() >= 2 && native[1] == L':' && is_letter(native[0])) {
    // Drive-rooted native form ("E:\..." from the fold, or the Windows
    // spelling as typed): the root step is the unambiguous "E:\", never the
    // drive-relative "E:" (which would track the per-drive current
    // directory). A bare "E:" operand stays one step.
    pos = (native.size() > 2 && is_sep(native[2])) ? 3 : 2;
    std::wstring root(native.substr(0, pos));
    steps.push_back({root, root});
  } else if (is_sep(native[0])) {
    if (native.size() >= 2 && is_sep(native[1])) {
      // UNC: "\\<server>\<share>" is the root component.
      size_t p = 2;
      while (p < native.size() && !is_sep(native[p])) ++p;
      if (p < native.size()) {
        ++p;
        while (p < native.size() && !is_sep(native[p])) ++p;
      }
      pos = p;
    } else {
      pos = 1;
    }
    std::wstring root(native.substr(0, pos));
    steps.push_back({root, root});
  }

  std::wstring prefix;
  while (pos < native.size()) {
    while (pos < native.size() && is_sep(native[pos])) ++pos;
    const size_t start = pos;
    while (pos < native.size() && !is_sep(native[pos])) ++pos;
    if (start == pos) break;
    prefix = std::wstring(native.substr(0, pos));
    steps.push_back({prefix, std::wstring(native.substr(start, pos - start))});
  }
  return steps;
}

// Root steps are the ones native_walk_steps pushed with name == prefix
// in a root spelling: "E:\", "E:", "\\srv\share", or "/".
auto step_is_root(const WalkStep& step) -> bool {
  if (step.name != step.prefix) return false;
  const auto& name = step.name;
  if (name == L"/") return true;
  if (name.size() == 2 && name[1] == L':') return true;
  if (!name.empty() && name.back() == L'\\') return true;
  if (name.size() > 2 && name[0] == L'\\' && name[1] == L'\\') return true;
  return false;
}

// 10-character mode string in xstrmode() shape. Windows has no mode
// bits: r is always set, w follows the read-only attribute, and x is
// set for directories and executable file extensions. Documented
// approximation (see REPORT.md).
auto mode_string(const StepInfo& info) -> std::string {
  const bool is_dir =
      (info.attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 && !info.is_symlink;
  std::string md;
  md += info.is_symlink ? 'l' : (is_dir ? 'd' : '-');
  if (info.is_symlink) return md + "rwxrwxrwx";
  const bool readonly = (info.attrs & FILE_ATTRIBUTE_READONLY) != 0;
  bool exec = is_dir;
  if (!exec) {
    const auto& name = info.name;
    const auto dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) {
      std::wstring ext = name.substr(dot);
      for (auto& ch : ext) {
        if (ch >= L'A' && ch <= L'Z') {
          ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
      }
      exec = ext == L".exe" || ext == L".com" || ext == L".bat" ||
             ext == L".cmd" || ext == L".ps1" || ext == L".msi";
    }
  }
  md += 'r';
  md += readonly ? '-' : 'w';
  md += exec ? 'x' : '-';
  md += "r-";
  md += exec ? 'x' : '-';
  md += "r-";
  md += exec ? 'x' : '-';
  return md;
}

auto account_label(const Win32AccountInfo& account) -> std::string {
  return account.name.empty() ? account.id : account.name;
}

auto run(const Config& cfg) -> int {
  int status = 0;
  for (const auto& path : cfg.paths) {
    safePrintLn("");
    safePrintLn(path);

    // #1237: the operand may use any accepted POSIX drive spelling
    // ("/e/Data", "/cygdrive/e/Data", "/mnt/e/Data").  Probing the raw
    // text's components with GetFileAttributesW resolved every one of them
    // against the CURRENT drive, so all dialect forms reported a broken
    // chain.  The operand folds once through the shared make_api_path_operand
    // boundary and the walk runs on the folded native spelling; the header
    // above still prints the operand exactly as the user typed it.
    const auto operand = native_path::make_api_path_operand(path);

    const auto steps = native_walk_steps(operand.normalized);

    // Probe the whole chain first (stopping at the first missing
    // component) so owner columns can be width-aligned per path.
    std::vector<StepInfo> infos;
    for (size_t i = 0; i < steps.size(); ++i) {
      StepInfo info = probe_step(steps[i].prefix, cfg.owners);
      info.name = steps[i].name;
      info.is_root = step_is_root(steps[i]);
      if (info.is_root) info.is_mount = true;
      infos.push_back(std::move(info));
      if (!infos.back().exists) break;
    }

    size_t owner_width = 0;
    size_t group_width = 0;
    if (cfg.owners) {
      for (const auto& info : infos) {
        if (!info.exists) continue;
        owner_width =
            std::max(owner_width, account_label(info.accounts.owner).size());
        group_width =
            std::max(group_width, account_label(info.accounts.group).size());
      }
    }

    for (size_t i = 0; i < infos.size(); ++i) {
      const auto& info = infos[i];
      const std::string indent(3 + (i > 0 ? i - 1 : 0), ' ');
      const std::string name = wstring_to_utf8(info.name);

      if (!info.exists) {
        // util-linux returns failure for the whole run when any
        // component is missing.
        status = 1;
        if (cfg.vertical) {
          safePrint("f");
          safePrint(indent);
          safePrint(" " + name);
        } else {
          safePrint(indent);
          safePrint("f " + name);
        }
        safePrintLn(" (No such file or directory)");
        break;
      }

      char glyph = 'f';
      if (info.is_symlink) {
        glyph = 's';
      } else if (info.attrs & FILE_ATTRIBUTE_DIRECTORY) {
        glyph = 'd';
      }
      if (cfg.mountpoints && info.is_mount) glyph = 'D';

      std::string block;
      if (cfg.modes) {
        block = mode_string(info);
        if (cfg.mountpoints && info.is_mount && !block.empty()) {
          block[0] = 'D';
        }
      } else {
        block = std::string(1, glyph);
      }
      if (cfg.owners) {
        std::string owner = account_label(info.accounts.owner);
        std::string group = account_label(info.accounts.group);
        owner.resize(owner_width, ' ');
        group.resize(group_width, ' ');
        block += " " + owner + " " + group;
      }
      if (cfg.context) block += " ?";

      if (cfg.vertical) {
        safePrint(block);
        safePrint(indent);
      } else {
        safePrint(indent);
        safePrint(block);
      }
      safePrint(" " + name);
      if (info.is_symlink && info.target.has_value()) {
        safePrintLn(" -> " + *info.target);
      } else {
        safePrintLn("");
      }
    }
  }
  return status;
}

}  // namespace namei_pipeline

REGISTER_COMMAND(namei, "namei", "namei [OPTION]... PATH...",
                 "Call namei on each PATH, showing any symlink resolution.\n"
                 "Displays the resolution steps for each path component.",
                 "  namei \/usr\/bin\/ls\n"
                 "  namei -l /path/to/file",
                 "readlink(1), realpath(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", NAMEI_OPTIONS) {
  using namespace namei_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("namei: missing operand");
      safeErrorPrintLn("Try 'namei --help' for more information.");
      return 1;
    }
    cp::report_error(cfg_result, L"namei");
    return 1;
  }

  return run(*cfg_result);
}
