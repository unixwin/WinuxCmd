/*
 *  Copyright ? 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: install.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for install.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright ? 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionType;

auto constexpr INSTALL_OPTIONS = std::array{
    OPTION("-b", "--backup", "make a backup of each existing destination file",
           BOOL_TYPE),
    OPTION("-c", "", "ignored (for compatibility with old Unix versions)",
           BOOL_TYPE),
    OPTION("-C", "", "ignored (for compatibility with old Unix versions)",
           BOOL_TYPE),
    OPTION("-d", "--directory", "treat all arguments as directory names",
           BOOL_TYPE),
    OPTION("-D", "", "create all leading components of DEST except the last",
           BOOL_TYPE),
    OPTION("-g", "--group", "set group ownership", STRING_TYPE),
    OPTION("-m", "--mode", "set permission mode", STRING_TYPE),
    OPTION("-o", "--owner", "set ownership", STRING_TYPE),
    OPTION("-p", "--preserve-timestamps",
           "apply access/modification times of SOURCE files", BOOL_TYPE),
    OPTION("-s", "--strip", "strip symbol tables", BOOL_TYPE),
    OPTION("", "--strip-program", "program used to strip binaries",
           STRING_TYPE),
    OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
    OPTION("-t", "--target-directory", "specify the destination directory",
           STRING_TYPE),
    OPTION("-T", "--no-target-directory",
           "do not treat the last operand specially when it is a directory",
           BOOL_TYPE),
    OPTION("-v", "--verbose",
           "print the name of each directory as it is created", BOOL_TYPE),
    OPTION("", "--preserve-context", "preserve SELinux security context",
           BOOL_TYPE),
    OPTION("-Z", "",
           "set SELinux security context of destination files to default",
           BOOL_TYPE)};

namespace install_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool backup = false;
  bool directory_mode = false;
  bool preserve_timestamps = false;
  bool strip = false;
  bool verbose = false;
  bool create_leading_dirs = false;
  bool no_target_directory = false;
  bool preserve_context = false;
  bool default_context = false;
  std::string backup_suffix = "~";
  std::string group;
  std::string mode;
  std::string owner;
  std::string strip_program;
  std::string target_dir;
  SmallVector<std::string, 64> sources;
};

auto build_config(const CommandContext<INSTALL_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.backup = ctx.get<bool>("--backup", false) || ctx.get<bool>("-b", false);
  cfg.directory_mode =
      ctx.get<bool>("--directory", false) || ctx.get<bool>("-d", false);
  cfg.preserve_timestamps = ctx.get<bool>("--preserve-timestamps", false) ||
                            ctx.get<bool>("-p", false);
  cfg.strip = ctx.get<bool>("--strip", false) || ctx.get<bool>("-s", false);
  cfg.verbose = ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  cfg.create_leading_dirs = ctx.get<bool>("-D", false);
  cfg.no_target_directory = ctx.get<bool>("-T", false) ||
                            ctx.get<bool>("--no-target-directory", false);
  cfg.preserve_context = ctx.get<bool>("--preserve-context", false);
  cfg.default_context = ctx.get<bool>("-Z", false);

  auto group_opt = ctx.get<std::string>("--group", "");
  if (group_opt.empty()) {
    group_opt = ctx.get<std::string>("-g", "");
  }
  cfg.group = group_opt;

  auto mode_opt = ctx.get<std::string>("--mode", "");
  if (mode_opt.empty()) {
    mode_opt = ctx.get<std::string>("-m", "");
  }
  cfg.mode = mode_opt;

  auto owner_opt = ctx.get<std::string>("--owner", "");
  if (owner_opt.empty()) {
    owner_opt = ctx.get<std::string>("-o", "");
  }
  cfg.owner = owner_opt;

  auto suffix_opt = ctx.get<std::string>("--suffix", "");
  if (suffix_opt.empty()) {
    suffix_opt = ctx.get<std::string>("-S", "");
  }
  if (!suffix_opt.empty()) {
    cfg.backup_suffix = suffix_opt;
  }

  cfg.strip_program = ctx.get<std::string>("--strip-program", "");

  auto target_opt = ctx.get<std::string>("--target-directory", "");
  if (target_opt.empty()) {
    target_opt = ctx.get<std::string>("-t", "");
  }
  cfg.target_dir = target_opt;

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.sources.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    cfg.sources.push_back(file_arg);
  }

  if (cfg.sources.empty()) {
    return std::unexpected("missing file operand");
  }

  if (!cfg.target_dir.empty()) {
    cfg.sources.push_back(cfg.target_dir);
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  if (cfg.directory_mode) {
    for (const auto& dir : cfg.sources) {
      if (cfg.verbose) {
        safePrint("install: creating directory '");
        safePrint(dir);
        safePrintLn("'");
      }

      if (!CreateDirectoryA(dir.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
          safePrint("install: cannot create directory '");
          safePrint(dir);
          safePrintLn("'");
          return 1;
        }
      }
    }
    return 0;
  }

  if (cfg.sources.size() < 2) {
    return 1;
  }

  SmallVector<std::string, 64> sources = cfg.sources;
  std::string target = sources.back();
  sources.pop_back();

  if (cfg.no_target_directory && sources.size() > 1) {
    safePrintLn("install: too many sources for -T/--no-target-directory");
    return 1;
  }

  DWORD attrs = GetFileAttributesA(target.c_str());
  bool target_is_dir = !cfg.no_target_directory &&
                       (attrs != INVALID_FILE_ATTRIBUTES) &&
                       (attrs & FILE_ATTRIBUTE_DIRECTORY);
  if (!target_is_dir && sources.size() > 1) {
    target_is_dir = true;
  }

  for (const auto& source : sources) {
    std::string dest = target;

    if (target_is_dir) {
      size_t last_slash = source.find_last_of("/\\");
      std::string filename = (last_slash != std::string::npos)
                                 ? source.substr(last_slash + 1)
                                 : source;
      if (!dest.empty() && dest.back() != '\\' && dest.back() != '/') {
        dest += "\\";
      }
      dest += filename;
    }

    if (cfg.create_leading_dirs) {
      std::filesystem::path dest_path(dest);
      auto parent = dest_path.parent_path();
      if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
      }
    }

    if (cfg.backup) {
      DWORD dest_attrs = GetFileAttributesA(dest.c_str());
      if (dest_attrs != INVALID_FILE_ATTRIBUTES) {
        std::string backup_path = dest + cfg.backup_suffix;
        if (MoveFileExA(dest.c_str(), backup_path.c_str(),
                        MOVEFILE_REPLACE_EXISTING)) {
          if (cfg.verbose) {
            safePrint("created backup: ");
            safePrintLn(backup_path);
          }
        }
      }
    }

    if (cfg.verbose) {
      safePrint("installing: ");
      safePrint(source);
      safePrint(" -> ");
      safePrintLn(dest);
    }

    if (!CopyFileA(source.c_str(), dest.c_str(), FALSE)) {
      safePrint("install: cannot copy '");
      safePrint(source);
      safePrint("' to '");
      safePrint(dest);
      safePrintLn("'");
      return 1;
    }
  }

  return 0;
}

}  // namespace install_pipeline

REGISTER_COMMAND(
    install, "install",
    "install [OPTION]... [-T] SOURCE DEST\n"
    "  install [OPTION]... SOURCE... DIRECTORY\n"
    "  install [OPTION]... -t DIRECTORY SOURCE...",
    "Copy files and set attributes.\n"
    "\n"
    "Note: This is a simplified Windows implementation.\n"
    "Advanced features like mode, owner, group, strip, and SELinux\n"
    "context handling are tracked but not fully supported on Windows.",
    "  install source.txt dest.txt\n"
    "  install -b file.txt backup/\n"
    "  install -v src/*.txt /target/\n"
    "  install -d /tmp/dir",
    "cp(1), mv(1)", "WinuxCmd", "Copyright ? 2026 WinuxCmd", INSTALL_OPTIONS) {
  using namespace install_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"install");
    return 1;
  }

  return run(*cfg_result);
}
