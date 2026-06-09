/*
 *  Copyright © 2026 [caomengxuan666]
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
 *  - File: dir.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
    OPTION("-a", "--all", "do not ignore entries starting with ."),
    OPTION("-A", "--almost-all", "do not list implied . and .."),
    OPTION("-b", "--escape", "print C-style escapes for nongraphic characters"),
    OPTION("-B", "--ignore-backups", "do not list implied entries ending with ~"),
    OPTION("-C", "", "list entries by columns"),
    OPTION("-d", "--directory", "list directories themselves, not their contents"),
    OPTION("-F", "--classify", "append indicator (one of */=>@|) to entries"),
    OPTION("-g", "", "like -l, but do not list owner"),
    OPTION("-h", "--human-readable",
           "with -l and -s, print sizes like 1K 234M 2G etc."),
    OPTION("-i", "--inode", "print the index number of each file"),
    OPTION("-l", "", "use a long listing format"),
    OPTION("-m", "", "fill width with a comma separated list of entries"),
    OPTION("-n", "--numeric-uid-gid", "like -l, but list numeric user and group IDs"),
    OPTION("-o", "", "like -l, but do not list group information"),
    OPTION("-p", "", "append / indicator to directories"),
    OPTION("-q", "--hide-control-chars", "print ? instead of nongraphic characters"),
    OPTION("-Q", "--quote-name", "enclose entry names in double quotes"),
    OPTION("-r", "--reverse", "reverse order while sorting"),
    OPTION("-R", "--recursive", "list subdirectories recursively"),
    OPTION("-s", "--size", "print the allocated size of each file, in blocks"),
    OPTION("-S", "", "sort by file size, largest first"),
    OPTION("-t", "", "sort by time, newest first"),
    OPTION("-T", "--tabsize", "assume tab stops at each COLS instead of 8",
           STRING_TYPE),
    OPTION("-u", "", "with -lt: sort by, and show, access time"),
    OPTION("-U", "", "do not sort; list entries in directory order"),
    OPTION("-v", "", "natural sort of (version) numbers within text"),
    OPTION("-w", "--width", "assume screen is instead of COLS wide", STRING_TYPE),
    OPTION("-x", "", "list entries by lines across"),
    OPTION("-X", "", "sort alphabetically by entry extension"),
    OPTION("-1", "", "list one file per line"),
    OPTION("", "--sort", "sort by WORD: none (-U), size (-S), time (-t), version (-v), extension (-X)",
           STRING_TYPE),
    OPTION("", "--format", "across (-x), commas (-m), horizontal (-x), long (-l), single-column (-1), verbose (-l), vertical (-C)",
           STRING_TYPE),
    OPTION("", "--time", "show time as WORD instead of default: atime, access, use, ctime, status",
           STRING_TYPE),
    OPTION("", "--color", "colorize the output: always, auto, never", STRING_TYPE),
    OPTION("", "--group-directories-first", "group directories before files"),
    OPTION("", "--zero", "end each output line with NUL, not newline")};

namespace dir_pipeline {
namespace cp = core::pipeline;

auto quote_dir_windows_arg(const std::wstring& arg) -> std::wstring {
  if (arg.empty()) return L"\"\"";

  bool need_quote = arg.find_first_of(L" \t\"") != std::wstring::npos;
  if (!need_quote) return arg;

  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      backslashes = 0;
      out.push_back(c);
    }
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

auto build_dir_command_line(std::span<const std::wstring> args) -> std::wstring {
  std::wstring cmd_line = L"ls.exe";
  for (const auto& arg : args) {
    cmd_line.push_back(L' ');
    cmd_line += quote_dir_windows_arg(arg);
  }
  return cmd_line;
}

auto run(const CommandContext<DIR_OPTIONS.size()>& ctx) -> int {
  // Build ls arguments with -C (columns) as default
  SmallVector<std::wstring, 32> ls_args;
  ls_args.push_back(L"-C");  // Default to columns

  // Preserve the original argv surface so GNU dir options actually reach ls.
  for (const auto& arg : ctx.raw_args) {
    ls_args.push_back(utf8_to_wstring(std::string(arg)));
  }

  std::wstring cmd_line = build_dir_command_line(ls_args);

  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;

  if (!CreateProcessW(NULL, &cmd_line[0], NULL, NULL, TRUE, 0, NULL, NULL,
                      &si, &pi)) {
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
    "  -b, --escape               print C-style escapes for nongraphic characters\n"
    "  -B, --ignore-backups       do not list implied entries ending with ~\n"
    "  -C                         list entries by columns (default)\n"
    "  -d, --directory            list directories themselves, not their contents\n"
    "  -F, --classify             append indicator (one of */=>@|) to entries\n"
    "  -g                         like -l, but do not list owner\n"
    "  -h, --human-readable       with -l and -s, print sizes like 1K 234M 2G\n"
    "  -i, --inode                print the index number of each file\n"
    "  -l                         use a long listing format\n"
    "  -m                         fill width with a comma separated list of entries\n"
    "  -n, --numeric-uid-gid      like -l, but list numeric user and group IDs\n"
    "  -o                         like -l, but do not list group information\n"
    "  -p                         append / indicator to directories\n"
    "  -q, --hide-control-chars   print ? instead of nongraphic characters\n"
    "  -Q, --quote-name           enclose entry names in double quotes\n"
    "  -r, --reverse              reverse order while sorting\n"
    "  -R, --recursive            list subdirectories recursively\n"
    "  -s, --size                 print the allocated size of each file, in blocks\n"
    "  -S                         sort by file size, largest first\n"
    "  -t                         sort by time, newest first\n"
    "  -u                         with -lt: sort by, and show, access time\n"
    "  -U                         do not sort; list entries in directory order\n"
    "  -v                         natural sort of (version) numbers within text\n"
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
