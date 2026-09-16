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
 *  - File: dircolors.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for dircolors.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr DIRCOLORS_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "--sh", "output Bourne shell code to set LS_COLORS"),
    // [GNU]
    OPTION("", "--bourne-shell", "output Bourne shell code to set LS_COLORS"),
    // [GNU]
    OPTION("-c", "--csh", "output C shell code to set LS_COLORS"),
    // [GNU]
    OPTION("", "--c-shell", "output C shell code to set LS_COLORS"),
    // [GNU]
    OPTION("-p", "--print-database", "output defaults"),
    // [GNU]
    OPTION("", "--print-ls-colors", "output fully escaped colors for display")};

namespace dircolors_pipeline {
namespace cp = core::pipeline;

// [GNU] print_database(): the compiled-in default database (Coreutils 9.11
// dircolors.hin, normalized like src/dcgen: blank runs collapsed to a
// single space, empty lines dropped).
constexpr std::string_view kDefaultDatabase =
    R"DB(# Configuration file for dircolors, a utility to help you set the
# LS_COLORS environment variable used by GNU ls with the --color option.

# Copyright (C) 1996-2026 Free Software Foundation, Inc.
# Copying and distribution of this file, with or without modification,
# are permitted provided the copyright notice and this notice are preserved.

#
# The keywords COLOR, OPTIONS, and EIGHTBIT (honored by the
# slackware version of dircolors) are recognized but ignored.

# Global config options can be specified before TERM or COLORTERM entries

# ===================================================================
# Terminal filters
# ===================================================================
# Below are TERM or COLORTERM entries, which can be glob patterns, which
# restrict following config to systems with matching environment variables.
COLORTERM ?*
TERM Eterm
TERM ansi
TERM *color*
TERM con[0-9]*x[0-9]*
TERM cons25
TERM console
TERM cygwin
TERM *direct*
TERM dtterm
TERM gnome
TERM hurd
TERM jfbterm
TERM konsole
TERM kterm
TERM linux
TERM linux-c
TERM mlterm
TERM putty
TERM rxvt*
TERM screen*
TERM st
TERM terminator
TERM tmux*
TERM vt100
TERM vt220
TERM xterm*

# ===================================================================
# Basic file attributes
# ===================================================================
# Below are the color init strings for the basic file types.
# One can use codes for 256 or more colors supported by modern terminals.
# The default color codes use the capabilities of an 8 color terminal
# with some additional attributes as per the following codes:
# Attribute codes:
# 00=none 01=bold 04=underscore 05=blink 07=reverse 08=concealed
# Text color codes:
# 30=black 31=red 32=green 33=yellow 34=blue 35=magenta 36=cyan 37=white
# Background color codes:
# 40=black 41=red 42=green 43=yellow 44=blue 45=magenta 46=cyan 47=white
#NORMAL 00	# no color code at all
#FILE 00	# regular file: use no color at all
RESET 0		# reset to "normal" color
DIR 01;34	# directory
LINK 01;36	# symbolic link.  (If you set this to 'target' instead of a
                # numerical value, the color is as for the file pointed to.)
MULTIHARDLINK 00	# regular file with more than one link
FIFO 40;33	# pipe
SOCK 01;35	# socket
DOOR 01;35	# door
BLK 40;33;01	# block device driver
CHR 40;33;01	# character device driver
ORPHAN 40;31;01 # symlink to nonexistent file, or non-stat'able file ...
MISSING 00      # ... and the files they point to
SETUID 37;41	# regular file that is setuid (u+s)
SETGID 30;43	# regular file that is setgid (g+s)
CAPABILITY 00	# regular file with capability (very expensive to lookup)
STICKY_OTHER_WRITABLE 30;42 # dir that is sticky and other-writable (+t,o+w)
OTHER_WRITABLE 34;42 # dir that is other-writable (o+w) and not sticky
STICKY 37;44	# dir with the sticky bit set (+t) and not other-writable

# This is for regular files with execute permission:
EXEC 01;32

# ===================================================================
# File extension attributes
# ===================================================================
# List any file extensions like '.gz' or '.tar' that you would like ls
# to color below. Put the suffix, a space, and the color init string.
# (and any comments you want to add after a '#').
# Suffixes are matched case insensitively, but if you define different
# init strings for separate cases, those will be honored.
#

# If you use DOS-style suffixes, you may want to uncomment the following:
#.cmd 01;32 # executables (bright green)
#.exe 01;32
#.com 01;32
#.btm 01;32
#.bat 01;32
# Or if you want to color scripts even if they do not have the
# executable bit actually set.
#.sh  01;32
#.csh 01;32

# archives or compressed (bright red)
.7z  01;31
.ace 01;31
.alz 01;31
.apk 01;31
.arc 01;31
.arj 01;31
.bz  01;31
.bz2 01;31
.cab 01;31
.cpio 01;31
.crate 01;31
.deb 01;31
.drpm 01;31
.dwm 01;31
.dz  01;31
.ear 01;31
.egg 01;31
.esd 01;31
.gz  01;31
.jar 01;31
.lha 01;31
.lrz 01;31
.lz  01;31
.lz4 01;31
.lzh 01;31
.lzma 01;31
.lzo 01;31
.pyz 01;31
.rar 01;31
.rpm 01;31
.rz  01;31
.sar 01;31
.swm 01;31
.t7z 01;31
.tar 01;31
.taz 01;31
.tbz 01;31
.tbz2 01;31
.tgz 01;31
.tlz 01;31
.txz 01;31
.tz  01;31
.tzo 01;31
.tzst 01;31
.udeb 01;31
.war 01;31
.whl 01;31
.wim 01;31
.xz  01;31
.z   01;31
.zip 01;31
.zoo 01;31
.zst 01;31

# image formats
.avif 01;35
.jpg 01;35
.jpeg 01;35
.jxl 01;35
.mjpg 01;35
.mjpeg 01;35
.gif 01;35
.bmp 01;35
.pbm 01;35
.pgm 01;35
.ppm 01;35
.tga 01;35
.xbm 01;35
.xpm 01;35
.tif 01;35
.tiff 01;35
.png 01;35
.svg 01;35
.svgz 01;35
.mng 01;35
.pcx 01;35
.mov 01;35
.mpg 01;35
.mpeg 01;35
.m2v 01;35
.mkv 01;35
.webm 01;35
.webp 01;35
.ogm 01;35
.mp4 01;35
.m4v 01;35
.mp4v 01;35
.vob 01;35
.qt  01;35
.nuv 01;35
.wmv 01;35
.asf 01;35
.rm  01;35
.rmvb 01;35
.flc 01;35
.avi 01;35
.fli 01;35
.flv 01;35
.gl 01;35
.dl 01;35
.xcf 01;35
.xwd 01;35
.yuv 01;35
.cgm 01;35
.emf 01;35

# https://wiki.xiph.org/MIME_Types_and_File_Extensions
.ogv 01;35
.ogx 01;35

# audio formats
.aac 00;36
.au 00;36
.flac 00;36
.m4a 00;36
.mid 00;36
.midi 00;36
.mka 00;36
.mp3 00;36
.mpc 00;36
.ogg 00;36
.ra 00;36
.wav 00;36

# https://wiki.xiph.org/MIME_Types_and_File_Extensions
.oga 00;36
.opus 00;36
.spx 00;36
.xspf 00;36

# backup files
*~ 00;90
*# 00;90
.bak 00;90
.crdownload 00;90
.dpkg-dist 00;90
.dpkg-new 00;90
.dpkg-old 00;90
.dpkg-tmp 00;90
.old 00;90
.orig 00;90
.part 00;90
.rej 00;90
.rpmnew 00;90
.rpmorig 00;90
.rpmsave 00;90
.swp 00;90
.tmp 00;90
.ucf-dist 00;90
.ucf-new 00;90
.ucf-old 00;90

#
# Subsequent TERM or COLORTERM entries, can be used to add / override
# config specific to those matching environment variables.
)DB";

// ===== [GNU] dc_parse_stream(): database parsing with TERM gating ==========

struct DbEntry {
  std::string key;
  std::string value;
};

// [GNU] slack_codes[]/ls_codes[]: long-form keywords mapped to the two
// letter LS_COLORS codes; matched case-insensitively.
inline constexpr std::array<std::pair<const char*, const char*>, 37>
    kSlackCodes{{
        {"NORMAL", "no"},
        {"NORM", "no"},
        {"FILE", "fi"},
        {"RESET", "rs"},
        {"DIR", "di"},
        {"LNK", "ln"},
        {"LINK", "ln"},
        {"SYMLINK", "ln"},
        {"ORPHAN", "or"},
        {"MISSING", "mi"},
        {"FIFO", "pi"},
        {"PIPE", "pi"},
        {"SOCK", "so"},
        {"BLK", "bd"},
        {"BLOCK", "bd"},
        {"CHR", "cd"},
        {"CHAR", "cd"},
        {"DOOR", "do"},
        {"EXEC", "ex"},
        {"LEFT", "lc"},
        {"LEFTCODE", "lc"},
        {"RIGHT", "rc"},
        {"RIGHTCODE", "rc"},
        {"END", "ec"},
        {"ENDCODE", "ec"},
        {"SUID", "su"},
        {"SETUID", "su"},
        {"SGID", "sg"},
        {"SETGID", "sg"},
        {"STICKY", "st"},
        {"OTHER_WRITABLE", "ow"},
        {"OWR", "ow"},
        {"STICKY_OTHER_WRITABLE", "tw"},
        {"OWT", "tw"},
        {"CAPABILITY", "ca"},
        {"MULTIHARDLINK", "mh"},
        {"CLRTOEOL", "cl"},
    }};

auto iequals(std::string_view a, std::string_view b) -> bool {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

enum class TermState { Global, TermNo, TermYes, TermSure };

// [GNU] parse_line(): first two whitespace-separated tokens; everything
// after '#' is a comment.
auto parse_db_line(std::string_view line, std::string_view& keyword,
                   std::string_view& arg) -> void {
  keyword = {};
  arg = {};
  size_t i = 0;
  auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
  while (i < line.size() && is_space(line[i])) ++i;
  if (i >= line.size() || line[i] == '#') return;
  const size_t kstart = i;
  while (i < line.size() && !is_space(line[i])) ++i;
  keyword = line.substr(kstart, i - kstart);
  while (i < line.size() && is_space(line[i])) ++i;
  if (i >= line.size() || line[i] == '#') return;
  const size_t astart = i;
  while (i < line.size() && line[i] != '#') ++i;
  // Trim trailing blanks of the argument.
  size_t aend = i;
  while (aend > astart && is_space(line[aend - 1])) --aend;
  arg = line.substr(astart, aend - astart);
}

// [GNU] dc_parse_stream(): parse one database stream into LS_COLORS
// entries.  TERM and COLORTERM directives gate the following entries on the
// corresponding environment variables (fnmatch globs).  Returns false on
// any malformed line.
auto parse_db_stream(std::istream& in, std::string_view filename, bool internal,
                     std::vector<DbEntry>& entries) -> bool {
  const char* term_env = std::getenv("TERM");
  const std::string term =
      (term_env != nullptr && *term_env != '\0') ? term_env : "none";
  const char* colorterm_env = std::getenv("COLORTERM");
  const std::string colorterm = colorterm_env != nullptr ? colorterm_env : "";

  TermState state = TermState::Global;
  bool ok = true;
  std::string line;
  intmax_t line_number = 0;
  const std::string shown_name =
      internal ? "<internal>" : std::string(filename);
  while (std::getline(in, line)) {
    ++line_number;
    std::string_view keyword, arg;
    parse_db_line(line, keyword, arg);
    if (keyword.empty()) continue;

    if (arg.empty()) {
      safeErrorPrintLn("dircolors: " + shown_name + ":" +
                       std::to_string(line_number) +
                       ": invalid line;  missing second token");
      ok = false;
      continue;
    }

    bool unrecognized = false;
    if (iequals(keyword, "TERM")) {
      if (state != TermState::TermSure) {
        state = wildcard_match(std::string(arg), term, true)
                    ? TermState::TermSure
                    : TermState::TermNo;
      }
    } else if (iequals(keyword, "COLORTERM")) {
      if (state != TermState::TermSure) {
        state = wildcard_match(std::string(arg), colorterm, true)
                    ? TermState::TermSure
                    : TermState::TermNo;
      }
    } else {
      if (state == TermState::TermSure) {
        state = TermState::TermYes;  // another {COLOR,}TERM can cancel
      }
      if (state != TermState::TermNo) {
        if (keyword.front() == '.') {
          entries.push_back({"*" + std::string(keyword), std::string(arg)});
        } else if (keyword.front() == '*') {
          entries.push_back({std::string(keyword), std::string(arg)});
        } else if (iequals(keyword, "OPTIONS") || iequals(keyword, "COLOR") ||
                   iequals(keyword, "EIGHTBIT")) {
          // Ignored slackware keywords.
        } else {
          const char* code = nullptr;
          for (const auto& [slack, ls_code] : kSlackCodes) {
            if (iequals(keyword, slack)) {
              code = ls_code;
              break;
            }
          }
          if (code == nullptr) {
            // [GNU-ext] also accept the two-letter LS_COLORS keys
            // directly (the compiled-in database below uses them).
            for (const auto& [slack, ls_code] : kSlackCodes) {
              if (iequals(keyword, ls_code)) {
                code = ls_code;
                break;
              }
            }
          }
          if (code != nullptr) {
            entries.push_back({code, std::string(arg)});
          } else {
            unrecognized = true;
          }
        }
      } else {
        unrecognized = true;
      }
    }

    if (unrecognized &&
        (state == TermState::TermSure || state == TermState::TermYes)) {
      safeErrorPrintLn("dircolors: " + shown_name + ":" +
                       std::to_string(line_number) + ": unrecognized keyword " +
                       std::string(keyword));
      ok = false;
    }
  }
  return ok;
}

// ===== [GNU] output generation ============================================

// [GNU] append_quoted(): in shell mode, '\'' becomes '\'''; '\\' and '^'
// toggle whether ':'/'=' need a backslash (for csh).
auto append_quoted(std::string& out, std::string_view str, bool print_ls_colors)
    -> void {
  bool need_backslash = true;
  for (char c : str) {
    if (!print_ls_colors) {
      switch (c) {
        case '\'':
          out += "'\\'";
          need_backslash = true;
          break;
        case '\\':
        case '^':
          need_backslash = !need_backslash;
          break;
        case ':':
        case '=':
          if (need_backslash) out.push_back('\\');
          need_backslash = true;
          break;
        default:
          need_backslash = true;
          break;
      }
    }
    out.push_back(c);
  }
}

// [GNU] append_entry().
auto append_entry(std::string& out, char prefix, std::string_view item,
                  std::string_view arg, bool print_ls_colors) -> void {
  if (print_ls_colors) {
    out += "\x1B[";
    out += arg;
    out += 'm';
  }
  if (prefix) out.push_back(prefix);
  append_quoted(out, item, print_ls_colors);
  out.push_back(print_ls_colors ? '\t' : '=');
  append_quoted(out, arg, print_ls_colors);
  if (print_ls_colors) out += "\x1B[0m";
  out.push_back(print_ls_colors ? '\n' : ':');
}

// [GNU] guess_shell_syntax(): csh/tcsh in $SHELL means C shell syntax.
// Deviation from GNU: when SHELL is unset we fall back to Bourne shell
// output instead of failing, because Windows has no SHELL convention.
auto guess_shell_is_csh() -> bool {
  const char* shell_env = std::getenv("SHELL");
  if (shell_env == nullptr || *shell_env == '\0') return false;
  std::string_view shell = shell_env;
  const size_t slash = shell.find_last_of("/\\");
  if (slash != std::string_view::npos) shell = shell.substr(slash + 1);
  return shell == "csh" || shell == "tcsh";
}

auto run(const CommandContext<DIRCOLORS_OPTIONS.size()>& ctx) -> int {
  bool bourne = ctx.get<bool>("-b", false) || ctx.get<bool>("--sh", false) ||
                ctx.get<bool>("--bourne-shell", false);
  bool csh = ctx.get<bool>("-c", false) || ctx.get<bool>("--csh", false) ||
             ctx.get<bool>("--c-shell", false);
  bool print_db =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--print-database", false);
  bool print_colors = ctx.get<bool>("--print-ls-colors", false);

  auto usage_fail = [](std::string_view message) {
    safeErrorPrintLn("dircolors: " + std::string(message));
    safeErrorPrintLn("Try 'dircolors --help' for more information.");
    return 1;
  };

  // [GNU] --print-database/--print-ls-colors are mutually exclusive with
  // the shell-syntax options and with each other.
  if ((print_db || print_colors) && (bourne || csh)) {
    return usage_fail(
        "the options to output non shell syntax,\nand to select a shell "
        "syntax are mutually exclusive");
  }
  if (print_db && print_colors) {
    return usage_fail(
        "options --print-database and --print-ls-colors are mutually "
        "exclusive");
  }

  // [GNU] at most one FILE operand, and none with --print-database.
  const size_t operand_limit = print_db ? 0 : 1;
  if (ctx.positionals.size() > operand_limit) {
    std::string msg =
        "extra operand '" + std::string(ctx.positionals[operand_limit]) + "'";
    safeErrorPrintLn("dircolors: " + msg);
    if (print_db) {
      safeErrorPrintLn(
          "file operands cannot be combined with --print-database (-p)");
    }
    safeErrorPrintLn("Try 'dircolors --help' for more information.");
    return 1;
  }

  if (print_db) {
    safePrint(kDefaultDatabase);
    return 0;
  }

  if (!bourne && !csh && !print_colors) {
    if (guess_shell_is_csh()) csh = true;
    bourne = !csh;
  }

  // Parse the compiled-in default database or the given FILE.
  std::vector<DbEntry> entries;
  bool ok = true;
  if (ctx.positionals.empty()) {
    std::istringstream db_stream{std::string(kDefaultDatabase)};
    ok = parse_db_stream(db_stream, "", /*internal=*/true, entries);
  } else {
    const std::string filename(ctx.positionals.front());
    if (filename == "-") {
      ok = parse_db_stream(std::cin, filename, /*internal=*/false, entries);
    } else {
      std::ifstream file(native_path::normalize_api_operand(filename));
      if (!file) {
        safeErrorPrintLn("dircolors: '" + filename +
                         "': " + portable_digest::open_error_reason(filename));
        return 1;
      }
      ok = parse_db_stream(file, filename, /*internal=*/false, entries);
    }
  }
  if (!ok) return 1;

  if (print_colors) {
    std::string out;
    for (const auto& e : entries) {
      append_entry(out, 0, e.key, e.value, true);
    }
    safePrint(out);
    return 0;
  }

  std::string out;
  for (const auto& e : entries) {
    append_entry(out, 0, e.key, e.value, false);
  }

  if (bourne) {
    safePrint("LS_COLORS='");
    safePrint(out);
    safePrint("';\nexport LS_COLORS\n");
  } else {
    safePrint("setenv LS_COLORS '");
    safePrint(out);
    safePrint("'\n");
  }
  return 0;
}

}  // namespace dircolors_pipeline

REGISTER_COMMAND(
    dircolors, "dircolors", "dircolors [OPTION]... [FILE]",
    "Output commands to set the LS_COLORS environment variable.\n"
    "\n"
    "Determine format of output:\n"
    "  -b, --sh           output Bourne shell code to set LS_COLORS (default)\n"
    "  -c, --csh          output C shell code to set LS_COLORS\n"
    "  -p, --print-database  output defaults\n"
    "      --print-ls-colors  output fully escaped colors for display\n"
    "\n"
    "If FILE is specified, read it to determine which colors to use for which\n"
    "file types and extensions.  Otherwise, a precompiled database is used.\n"
    "\n"
    "Note: On Windows, dircolors outputs color codes compatible with ls "
    "--color.",
    "  dircolors         output Bourne shell code (default)\n"
    "  dircolors -c      output C shell code\n"
    "  dircolors -p      print default color database\n"
    "  eval \"$(dircolors)\"  set LS_COLORS in current shell",
    "ls(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", DIRCOLORS_OPTIONS) {
  using namespace dircolors_pipeline;
  using namespace core::pipeline;

  return run(ctx);
}
