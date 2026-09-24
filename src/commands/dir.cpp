// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for dir (ls with column default).
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

auto constexpr DIR_OPTIONS = std::array{
    // [GNU]
    OPTION("-a", "--all", "do not ignore entries starting with ."),
    // [GNU]
    OPTION("-A", "--almost-all", "do not list implied . and .."),
    // [GNU]
    OPTION("-b", "--escape", "print C-style escapes for nongraphic characters"),
    // [GNU]
    OPTION("-B", "--ignore-backups",
           "do not list implied entries ending with ~"),
    // [GNU]
    OPTION("-C", "", "list entries by columns"),
    // [GNU]
    OPTION("-d", "--directory",
           "list directories themselves, not their contents"),
    // [GNU]
    OPTION("-F", "--classify", "append indicator (one of */=>@|) to entries"),
    // [GNU]
    OPTION("-g", "", "like -l, but do not list owner"),
    // [GNU]
    OPTION("-h", "--human-readable",
           "with -l and -s, print sizes like 1K 234M 2G etc."),
    // [GNU]
    OPTION("-i", "--inode", "print the index number of each file"),
    // [GNU]
    OPTION("-l", "", "use a long listing format"),
    // [GNU]
    OPTION("", "--long", "use a long listing format"),
    // [EXT]
    OPTION("", "--long-list", "use a long listing format"),
    // [GNU]
    OPTION("-m", "", "fill width with a comma separated list of entries"),
    // [GNU]
    OPTION("-n", "--numeric-uid-gid",
           "like -l, but list numeric user and group IDs"),
    // [GNU]
    OPTION("-o", "", "like -l, but do not list group information"),
    // [GNU]
    OPTION("-p", "", "append / indicator to directories"),
    // [GNU]
    OPTION("-q", "--hide-control-chars",
           "print ? instead of nongraphic characters"),
    // [GNU]
    OPTION("-Q", "--quote-name", "enclose entry names in double quotes"),
    // [GNU]
    OPTION("-r", "--reverse", "reverse order while sorting"),
    // [GNU]
    OPTION("-R", "--recursive", "list subdirectories recursively"),
    // [GNU]
    OPTION("-s", "--size", "print the allocated size of each file, in blocks"),
    // [GNU]
    OPTION("-S", "", "sort by file size, largest first"),
    // [GNU]
    OPTION("-t", "", "sort by time, newest first"),
    // [GNU]
    OPTION("-T", "--tabsize", "assume tab stops at each COLS instead of 8",
           STRING_TYPE),
    // [GNU]
    OPTION("-u", "", "with -lt: sort by, and show, access time"),
    // [GNU]
    OPTION("-U", "", "do not sort; list entries in directory order"),
    // [GNU]
    OPTION("-v", "", "natural sort of (version) numbers within text"),
    // [GNU]
    OPTION("-w", "--width", "assume screen is instead of COLS wide",
           STRING_TYPE),
    // [GNU]
    OPTION("-x", "", "list entries by lines across"),
    // [GNU]
    OPTION("-X", "", "sort alphabetically by entry extension"),
    // [GNU]
    OPTION("-1", "", "list one file per line"),
    // [GNU]
    OPTION("", "--sort",
           "sort by WORD: none (-U), size (-S), time (-t), version (-v), "
           "extension (-X)",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--format",
           "across (-x), commas (-m), horizontal (-x), long (-l), "
           "single-column (-1), verbose (-l), vertical (-C)",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--time",
           "show time as WORD instead of default: atime, access, use, ctime, "
           "status",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--color", "colorize the output: always, auto, never",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--group-directories-first", "group directories before files"),
    // [GNU] --author: show author in long format
    OPTION("", "--author", "show author in long format"),
    // [GNU] --block-size: scale sizes by SIZE
    OPTION("", "--block-size", "scale sizes by SIZE", STRING_TYPE),
    // [GNU] --context: print any security context of each file
    // [DIFFERS]
    OPTION("-Z", "--context", "print any security context of each file"),
    // [GNU] --dereference: when showing file information for a symbolic link,
    // show information for the file the link references
    OPTION("-L", "--dereference",
           "when showing file information for a symbolic link, show "
           "information for the file the link references"),
    // [GNU] --dereference-command-line: follow symlinks listed on the command
    // line
    OPTION("-H", "--dereference-command-line",
           "follow symlinks listed on the command line"),
    // [GNU] --dereference-command-line-symlink-to-dir: follow each command-line
    // symlink to a directory
    OPTION("", "--dereference-command-line-symlink-to-dir",
           "follow each command-line symlink to a directory"),
    // [GNU]
    OPTION("", "--dereference-command-line-symlinks-to-dir",
           "follow each command-line symlink to a directory"),
    // [GNU] --dired: generate output designed for Emacs dired mode
    OPTION("-D", "--dired", "generate output designed for Emacs dired mode"),
    // [GNU] --file-type: append file type indicators, without '*'
    OPTION("", "--file-type", "append file type indicators, without *"),
    // [GNU] --hide: do not list implied entries matching PATTERN
    OPTION("", "--hide", "do not list implied entries matching PATTERN",
           STRING_TYPE),
    // [GNU] --hyperlink: hyperlink file names when outputting to a terminal
    OPTION("", "--hyperlink",
           "hyperlink file names when outputting to a terminal",
           OPTIONAL_STRING_TYPE),
    // [GNU] --ignore: do not list implied entries matching PATTERN
    OPTION("-I", "--ignore", "do not list implied entries matching PATTERN",
           STRING_TYPE),
    // [GNU] --indicator-style: append indicator using WORD
    OPTION("", "--indicator-style", "append indicator using WORD", STRING_TYPE),
    // [GNU] --literal: print entry names without quoting
    OPTION("-N", "--literal", "print entry names without quoting"),
    // [GNU] --no-group: in a long listing, don't print group names
    OPTION("-G", "--no-group", "in a long listing, don't print group names"),
    // [GNU] --quoting-style: use quoting style WORD
    OPTION("", "--quoting-style", "use quoting style WORD", STRING_TYPE),
    // [GNU] --show-control-chars: show nongraphic characters as-is in file
    // names
    OPTION("", "--show-control-chars",
           "show nongraphic characters as-is in file names"),
    // [GNU] --si: like -h, but use powers of 1000 not 1024
    OPTION("", "--si", "like -h, but use powers of 1000 not 1024"),
    // [GNU] --time-style: time/date format with -l
    OPTION("", "--time-style",
           "time/date format with -l (e.g. full-iso, long-iso, iso, locale, "
           "+FORMAT)",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--full-time", "like -l --time-style=full-iso"),
    // [GNU] -c: with -lt: sort by, and show, ctime; otherwise sort by ctime
    OPTION("-c", "",
           "with -lt: sort by, and show, ctime; otherwise: sort by ctime, "
           "newest first"),
    // [GNU] -f: list all entries in directory order
    OPTION("-f", "", "list all entries in directory order"),
    // [GNU] -k: default to 1024-byte blocks for file system usage
    OPTION("-k", "--kibibytes",
           "default to 1024-byte blocks for file system usage"),
    // [GNU]
    OPTION("", "--zero", "end each output line with NUL, not newline")};

namespace dir_pipeline {
namespace cp = core::pipeline;

auto build_dir_command_line(std::span<const std::wstring> args)
    -> std::wstring {
  std::wstring cmd_line = L"ls.exe";
  for (const auto& arg : args) {
    append_windows_command_arg(cmd_line, arg);
  }
  return cmd_line;
}

auto run(const CommandContext<DIR_OPTIONS.size()>& ctx) -> int {
  // [GNU] dir is equivalent to `ls -C -b': list entries in columns and
  // quote nongraphic characters with C-style escapes by default.
  std::vector<std::string> ls_arg_storage;
  ls_arg_storage.push_back("-C");
  ls_arg_storage.push_back("-b");

  // Preserve the original argv surface so GNU dir options actually reach ls.
  for (const auto& arg : ctx.raw_args) {
    ls_arg_storage.emplace_back(arg);
  }

  if (CommandRegistry::hasCommand("ls")) {
    std::vector<std::string_view> ls_arg_views;
    ls_arg_views.reserve(ls_arg_storage.size());
    for (const auto& arg : ls_arg_storage) {
      ls_arg_views.emplace_back(arg);
    }
    return CommandRegistry::dispatch("ls", ls_arg_views);
  }

  SmallVector<std::wstring, 32> ls_args;
  for (const auto& arg : ls_arg_storage) {
    ls_args.push_back(utf8_to_wstring(arg));
  }

  std::wstring cmd_line = build_dir_command_line(ls_args);

  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;

  if (!CreateProcessW(NULL, &cmd_line[0], NULL, NULL, TRUE, 0, NULL, NULL, &si,
                      &pi)) {
    safeErrorPrintLn("dir: failed to execute ls");
    return 1;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return static_cast<int>(exit_code);
}

}  // namespace dir_pipeline

REGISTER_COMMAND(
    dir, "dir", "dir [OPTION]... [FILE]...",
    "List information about files (the current directory by default).\n"
    "dir is equivalent to ls -C; it lists files in columns.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -a, --all                  do not ignore entries starting with .\n"
    "  -A, --almost-all           do not list implied . and ..\n"
    "  -b, --escape               print C-style escapes for nongraphic "
    "characters\n"
    "  -B, --ignore-backups       do not list implied entries ending with ~\n"
    "  -C                         list entries by columns (default)\n"
    "  -d, --directory            list directories themselves, not their "
    "contents\n"
    "  -F, --classify             append indicator (one of */=>@|) to entries\n"
    "  -g                         like -l, but do not list owner\n"
    "  -h, --human-readable       with -l and -s, print sizes like 1K 234M 2G\n"
    "  -i, --inode                print the index number of each file\n"
    "  -l                         use a long listing format\n"
    "  -m                         fill width with a comma separated list of "
    "entries\n"
    "  -n, --numeric-uid-gid      like -l, but list numeric user and group "
    "IDs\n"
    "  -o                         like -l, but do not list group information\n"
    "  -p                         append / indicator to directories\n"
    "  -q, --hide-control-chars   print ? instead of nongraphic characters\n"
    "  -Q, --quote-name           enclose entry names in double quotes\n"
    "  -r, --reverse              reverse order while sorting\n"
    "  -R, --recursive            list subdirectories recursively\n"
    "  -s, --size                 print the allocated size of each file, in "
    "blocks\n"
    "  -S                         sort by file size, largest first\n"
    "  -t                         sort by time, newest first\n"
    "  -u                         with -lt: sort by, and show, access time\n"
    "  -U                         do not sort; list entries in directory "
    "order\n"
    "  -v                         natural sort of (version) numbers within "
    "text\n"
    "  -w, --width=COLS           assume screen is COLS wide\n"
    "  -x                         list entries by lines across\n"
    "  -X                         sort alphabetically by entry extension\n"
    "  -1                         list one file per line\n"
    "\n"
    "dir is a wrapper around ls with -C (columns) as the default format.",
    "  dir            list files in columns\n"
    "  dir -l         list in long format\n"
    "  dir -a         show hidden files\n"
    "  dir -R         recursive listing",
    "ls(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", DIR_OPTIONS) {
  using namespace dir_pipeline;
  using namespace core::pipeline;

  return run(ctx);
}
